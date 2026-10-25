// SPDX-FileCopyrightText: 2026 DrEvan
// SPDX-License-Identifier: GPL-3.0-or-later

/*
 * display.cpp - TFT_eSPI + LVGL display driver (ST7796U, 480x320 landscape)
 *
 * The backlight (GPIO14) is on/off only: PWM on the backlight disturbs the
 * capacitive touch controller. Full-screen LVGL draw buffer in PSRAM.
 */

#include "display.h"
#include <TFT_eSPI.h>
#include <Arduino.h>
#include <math.h>
#include "fault.h"

#define PIN_BL   14
#define PIN_RST   8

static TFT_eSPI tft;
static lv_disp_draw_buf_t draw_buf;
static lv_color_t *lvgl_buf = nullptr;

// ── Screen brightness ────────────────────────────────────────
// Levels below 100 % use a black, non-clickable overlay on lv_layer_sys()
// whose opacity sets apparent brightness; touches pass through it. Fades step
// the overlay. Auto-dim switches the backlight off; the first touch wakes it.
#define BL_STEP_MS      40         // overlay steps (each step redraws the screen)

static lv_obj_t *s_dim_ov = nullptr; // black overlay on lv_layer_sys()
static int       s_ov_opa = -1;

static uint8_t  s_user_pct  = 100;   // user setting 10-100
static uint8_t  s_dim_min   = 0;     // auto-dim after N idle minutes, 0 = never
static float    s_cur       = 0.0f;  // current output, % (fades toward s_target)
static float    s_target    = 0.0f;
static float    s_rate      = 0.0f;  // % per BL_STEP_MS
static bool     s_dimmed    = false;
static uint32_t s_last_act  = 0;
static uint32_t s_last_step = 0;
static uint32_t s_trips_seen = 0;

// Perceived brightness -> overlay opacity: square law with a small floor, so
// the low end of the slider still changes visibly and 10 % stays readable.
static void bl_write(float pct) {
    s_cur = pct;
    digitalWrite(PIN_BL, pct > 0.0f ? HIGH : LOW);
    if (!s_dim_ov) return;
    int opa = 0;
    if (pct > 0.0f && pct < 100.0f) {
        float x = pct / 100.0f;
        float lum = 0.02f + 0.98f * x * x;          // target light output
        float val = powf(lum, 1.0f / 2.2f);         // pixel scale (display gamma ~2.2)
        opa = (int)((1.0f - val) * 255.0f + 0.5f) & ~3;   // 4-step quantum: fewer redraws
    }
    if (opa == s_ov_opa) return;
    s_ov_opa = opa;
    if (opa == 0) { lv_obj_add_flag(s_dim_ov, LV_OBJ_FLAG_HIDDEN); return; }
    lv_obj_set_style_bg_opa(s_dim_ov, (lv_opa_t)opa, LV_PART_MAIN);
    lv_obj_clear_flag(s_dim_ov, LV_OBJ_FLAG_HIDDEN);
}

static void dim_overlay_create() {
    s_dim_ov = lv_obj_create(lv_layer_sys());
    lv_obj_remove_style_all(s_dim_ov);
    lv_obj_set_size(s_dim_ov, 480, 320);
    lv_obj_set_pos(s_dim_ov, 0, 0);
    lv_obj_set_style_bg_color(s_dim_ov, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_dim_ov, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(s_dim_ov, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    s_ov_opa = 255;
}

static void bl_fade_to(float pct, uint32_t ms) {
    s_target = pct;
    float steps = ms / (float)BL_STEP_MS;
    s_rate = steps < 1.0f ? 1000.0f : fabsf(pct - s_cur) / steps;
    if (s_rate < 0.01f) s_rate = 0.01f;
}

static void bl_init() {
    pinMode(PIN_BL, OUTPUT);
    digitalWrite(PIN_BL, LOW);     // dark until display_fade_in()
    s_cur = s_target = 0.0f;
}

void display_set_brightness(uint8_t val) {   // 0 = off, >0 = user level
    bl_fade_to(val ? s_user_pct : 0, 0);
}

void display_set_level(uint8_t pct) {
    if (pct < 10) pct = 10;
    if (pct > 100) pct = 100;
    s_user_pct = pct;
    if (!s_dimmed) bl_fade_to(pct, 0);
}

uint8_t display_level() { return s_user_pct; }

void display_set_autodim(uint8_t minutes) {
    s_dim_min  = minutes;
    s_last_act = millis();
    if (!minutes && s_dimmed) { s_dimmed = false; bl_fade_to(s_user_pct, 0); }
}

void display_fade_in(uint32_t ms) {
    s_last_act = millis();
    s_dimmed   = false;
    bl_fade_to(s_user_pct, ms);
}

bool display_is_dimmed() { return s_dimmed; }

bool display_touch_activity() {
    s_last_act = millis();
    if (!s_dimmed) return false;
    s_dimmed = false;
    bl_fade_to(s_user_pct, 0);
    return true;           // this press only wakes the screen
}

void display_wake() { (void)display_touch_activity(); }

void display_tick() {
    uint32_t now = millis();

    // A new trip wakes the screen so the fault popup is readable.
    uint32_t tc = fault_trip_count();
    if (tc != s_trips_seen) { s_trips_seen = tc; display_wake(); }

    if (s_dim_min && !s_dimmed && now - s_last_act >= (uint32_t)s_dim_min * 60000u) {
        s_dimmed = true;
        bl_fade_to(0, 0);          // backlight off; first touch turns it back on
    }

    if (now - s_last_step < BL_STEP_MS) return;
    s_last_step = now;
    if (s_cur == s_target) return;
    float n = s_cur < s_target ? fminf(s_cur + s_rate, s_target) : fmaxf(s_cur - s_rate, s_target);
    bl_write(n);
}

// ── LVGL flush callback ──────────────────────────────────────
static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p) {
    uint32_t w = area->x2 - area->x1 + 1;
    uint32_t h = area->y2 - area->y1 + 1;
    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushColors((uint16_t*)color_p, w * h, false);
    tft.endWrite();
    lv_disp_flush_ready(drv);
}

// ── Public init ──────────────────────────────────────────────
void display_init() {
    bl_init();  // backlight off

    // Hard reset: 200 ms low, 200 ms settle
    pinMode(PIN_RST, OUTPUT);
    digitalWrite(PIN_RST, HIGH); delay(50);
    digitalWrite(PIN_RST, LOW);  delay(200);
    digitalWrite(PIN_RST, HIGH); delay(200);

    tft.init();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);

    // Backlight stays dark here; main.cpp calls display_fade_in() once the
    // splash screen has been created, so the first visible frame fades up.

    lv_init();

    lvgl_buf = (lv_color_t*) ps_malloc(sizeof(lv_color_t) * 480 * 320);
    if (!lvgl_buf) {
        Serial.println("[DISPLAY] FATAL: ps_malloc for draw buffer failed.");
        while (1) delay(1000);
    }
    lv_disp_draw_buf_init(&draw_buf, lvgl_buf, nullptr, 480 * 320);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res  = 480;
    disp_drv.ver_res  = 320;
    disp_drv.flush_cb = flush_cb;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);
    dim_overlay_create();          // brightness overlay (starts fully black)

    Serial.println("[DISPLAY] Init complete. 480×320, PSRAM draw buffer.");
}