// SPDX-FileCopyrightText: 2026 DrEvan
// SPDX-License-Identifier: GPL-3.0-or-later

/*
 * screen_splash.cpp - Boot splash, laid out to match keylight_splash.html.
 * Logo box, CCTRL mark and a 2.5 s load bar, then advances to SCR_MAIN.
 */

#include "screens/screen_splash.h"
#include "screens/screen_manager.h"
#include "ui_theme.h"
#include "lautus_logo.h"
#include "cctrl_logo.h"
#include <Arduino.h>

static lv_obj_t  *s_bar = nullptr;
static lv_timer_t *s_tmr = nullptr;

// ── Load bar tick ─────────────────────────────────────────────
static void splash_tick(lv_timer_t *) {
    if (!s_bar) return;
    int32_t v = lv_bar_get_value(s_bar);
    if (v >= 100) {
        lv_timer_del(s_tmr);
        s_tmr = nullptr;
        s_bar = nullptr;
        screen_manager_goto(SCR_MAIN);
        return;
    }
    lv_bar_set_value(s_bar, v + 2, LV_ANIM_ON);
}

// ── Corner bracket helper ─────────────────────────────────────
// L-shaped bracket from two 1px objects.
//   x, y   = corner tip (screen coords)
//   flip_x = arms extend left (right-side corners)
//   flip_y = arms extend up (bottom corners)
static void draw_corner(lv_obj_t *parent,
                        int x, int y, int arm,
                        bool flip_x, bool flip_y,
                        lv_opa_t opa)
{
    lv_obj_t *h = lv_obj_create(parent);
    lv_obj_set_size(h, arm, 1);
    lv_obj_set_style_bg_color(h, UI_COL_ACCENT, LV_PART_MAIN);
    lv_obj_set_style_bg_opa  (h, opa,           LV_PART_MAIN);
    lv_obj_set_style_border_width(h, 0, LV_PART_MAIN);
    lv_obj_set_style_radius      (h, 0, LV_PART_MAIN);
    lv_obj_set_pos(h, flip_x ? x - arm : x, flip_y ? y - 1 : y);
    lv_obj_clear_flag(h, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *v = lv_obj_create(parent);
    lv_obj_set_size(v, 1, arm);
    lv_obj_set_style_bg_color(v, UI_COL_ACCENT, LV_PART_MAIN);
    lv_obj_set_style_bg_opa  (v, opa,           LV_PART_MAIN);
    lv_obj_set_style_border_width(v, 0, LV_PART_MAIN);
    lv_obj_set_style_radius      (v, 0, LV_PART_MAIN);
    lv_obj_set_pos(v, flip_x ? x - 1 : x, flip_y ? y - arm : y);
    lv_obj_clear_flag(v, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
}

// ── 1px accent line helper ────────────────────────────────────
static void draw_accent_line(lv_obj_t *parent,
                             int x, int y, int w, lv_opa_t opa)
{
    lv_obj_t *l = lv_obj_create(parent);
    lv_obj_set_size(l, w, 1);
    lv_obj_set_pos(l, x, y);
    lv_obj_set_style_bg_color(l, UI_COL_ACCENT, LV_PART_MAIN);
    lv_obj_set_style_bg_opa  (l, opa,           LV_PART_MAIN);
    lv_obj_set_style_border_width(l, 0, LV_PART_MAIN);
    lv_obj_set_style_radius      (l, 0, LV_PART_MAIN);
    lv_obj_clear_flag(l, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
}

// ── Public ────────────────────────────────────────────────────
lv_obj_t* screen_splash_create() {

    lv_obj_t *scr = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x06080F), LV_PART_MAIN);
    lv_obj_set_style_bg_opa  (scr, LV_OPA_COVER,           LV_PART_MAIN);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // ── Bloom glow ────────────────────────────────────────────
    // Solid low-opacity ellipse standing in for the HTML radial gradient.
    lv_obj_t *bloom = lv_obj_create(scr);
    lv_obj_set_size(bloom, 340, 150);
    lv_obj_set_pos(bloom, (UI_W - 340) / 2, 134 - 75);   // center y = 134
    lv_obj_set_style_radius  (bloom, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bloom, UI_COL_ACCENT,    LV_PART_MAIN);
    lv_obj_set_style_bg_opa  (bloom, (lv_opa_t)20,     LV_PART_MAIN);
    lv_obj_set_style_border_width(bloom, 0, LV_PART_MAIN);
    lv_obj_clear_flag(bloom, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    // ── Corner brackets: 12px arms, 14px inset, 40% opacity ──
    const int    INS = 14, ARM = 12;
    const lv_opa_t BOPA = (lv_opa_t)102;   // 40%
    draw_corner(scr, INS,         INS,         ARM, false, false, BOPA);  // TL
    draw_corner(scr, UI_W - INS,  INS,         ARM, true,  false, BOPA);  // TR
    draw_corner(scr, INS,         UI_H - INS,  ARM, false, true,  BOPA);  // BL
    draw_corner(scr, UI_W - INS,  UI_H - INS,  ARM, true,  true,  BOPA);  // BR

    // ── Layout ────────────────────────────────────────────────
    // Stack: box(113) + 16 + CCTRL mark(20) + 20 + bar(1) = 170px,
    // centered vertically: (320 - 170) / 2 = 75
    const int LOGO_W = 320, LOGO_H = 87;
    const int PAD_H  = 32,  PAD_T = 14;
    const int BOX_W  = LOGO_W + 2 * PAD_H;   // 384
    const int BOX_H  = PAD_T + LOGO_H + 12;   // 113
    const int BOX_X  = (UI_W - BOX_W) / 2;   // 48
    const int BOX_Y  = 75;

    // Box border
    lv_obj_t *box = lv_obj_create(scr);
    lv_obj_set_size(box, BOX_W, BOX_H);
    lv_obj_set_pos(box, BOX_X, BOX_Y);
    lv_obj_set_style_bg_color   (box, lv_color_hex(0x06080F), LV_PART_MAIN);
    lv_obj_set_style_bg_opa     (box, LV_OPA_COVER,           LV_PART_MAIN);
    lv_obj_set_style_border_color(box, lv_color_hex(0x1A2540), LV_PART_MAIN);
    lv_obj_set_style_border_width(box, 1,                      LV_PART_MAIN);
    lv_obj_set_style_radius     (box, 0,                       LV_PART_MAIN);
    lv_obj_set_style_pad_all    (box, 0,                       LV_PART_MAIN);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    // Logo bitmap is pre-composited on #06080F
    lv_obj_t *img = lv_img_create(box);
    lv_img_set_src(img, &lautus_logo);
    lv_obj_set_pos(img, PAD_H, PAD_T);

    // Accent lines over the box top and bottom edges, inset 16px each side
    const int ACC_W  = BOX_W - 32;         // 352px
    const int ACC_X  = BOX_X + 16;         // 64
    const lv_opa_t AOPA = (lv_opa_t)128;  // 50%
    draw_accent_line(scr, ACC_X, BOX_Y,           ACC_W, AOPA);  // top
    draw_accent_line(scr, ACC_X, BOX_Y + BOX_H,   ACC_W, AOPA);  // bottom

    // ── CCTRL mark ────────────────────────────────────────────
    const int SUB_Y = BOX_Y + BOX_H + 16;
    lv_obj_t *mark = lv_img_create(scr);
    lv_img_set_src(mark, &cctrl_logo);
    lv_obj_set_pos(mark, (UI_W - cctrl_logo.header.w) / 2, SUB_Y);

    // ── Load bar ──────────────────────────────────────────────
    const int BAR_Y = SUB_Y + cctrl_logo.header.h + 20;   // mark height + 20px gap
    s_bar = lv_bar_create(scr);
    lv_obj_set_size(s_bar, 200, 1);
    lv_obj_set_pos(s_bar, (UI_W - 200) / 2, BAR_Y);
    lv_bar_set_value(s_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(0x0E1420), LV_PART_MAIN);
    lv_obj_set_style_bg_opa  (s_bar, LV_OPA_COVER,           LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bar, UI_COL_ACCENT,          LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa  (s_bar, (lv_opa_t)204,          LV_PART_INDICATOR);  // 80%
    lv_obj_set_style_radius  (s_bar, 0,                       LV_PART_MAIN);
    lv_obj_set_style_radius  (s_bar, 0,                       LV_PART_INDICATOR);

    // +2% every 50ms = 2500ms total
    s_tmr = lv_timer_create(splash_tick, 50, nullptr);

    return scr;
}
