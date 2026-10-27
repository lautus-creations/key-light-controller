// SPDX-FileCopyrightText: 2026 DrEvan
// SPDX-License-Identifier: GPL-3.0-or-later

/*
 * lock.cpp - screen lock overlay (long-press to unlock)
 */

#include "lock.h"
#include "ui_theme.h"
#include <Arduino.h>
#include "klc_log.h"
#include <lvgl.h>

static lv_obj_t   *s_ov    = nullptr;   // full-screen touch catcher on lv_layer_top()
static lv_obj_t   *s_hint  = nullptr;   // center card: text + progress bar
static lv_obj_t   *s_bar   = nullptr;
static lv_obj_t   *s_badge = nullptr;   // small "Locked" tag, always visible
static lv_timer_t *s_hide  = nullptr;
static uint32_t    s_press_ms = 0;
static bool        s_holding  = false;

static void hint_hide_cb(lv_timer_t *t) {
    s_hide = nullptr;
    if (s_hint && !s_holding) lv_obj_add_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
}

static void hint_show(uint32_t ms) {
    if (!s_hint) return;
    lv_obj_clear_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
    if (s_hide) lv_timer_del(s_hide);
    s_hide = lv_timer_create(hint_hide_cb, ms, nullptr);
    lv_timer_set_repeat_count(s_hide, 1);
}

static void ov_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        s_press_ms = millis();
        s_holding  = true;
        lv_bar_set_value(s_bar, 0, LV_ANIM_OFF);
        hint_show(60000);
    } else if (code == LV_EVENT_PRESSING && s_holding) {
        uint32_t held = millis() - s_press_ms;
        lv_bar_set_value(s_bar, (int32_t)(held * 100 / LOCK_HOLD_MS), LV_ANIM_OFF);
        if (held >= LOCK_HOLD_MS) { s_holding = false; lock_release(); }
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        if (!s_holding) return;
        s_holding = false;
        lv_bar_set_value(s_bar, 0, LV_ANIM_OFF);
        hint_show(1500);
    }
}

void lock_engage() {
    if (s_ov) return;
    s_ov = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_ov);
    lv_obj_set_size(s_ov, UI_W, UI_H);
    lv_obj_set_pos(s_ov, 0, 0);
    lv_obj_add_flag(s_ov, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_ov, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_ov, ov_cb, LV_EVENT_ALL, nullptr);

    // Sits exactly over the main screen's Lock button (bottom bar, right).
    s_badge = lv_obj_create(s_ov);
    lv_obj_set_size(s_badge, 120, UI_BAR_BTN_H);
    lv_obj_align(s_badge, LV_ALIGN_BOTTOM_RIGHT, -UI_SAFE, -(UI_BAR_H - UI_BAR_BTN_H) / 2);
    lv_obj_set_style_bg_color(s_badge, lv_color_hex(0x2A1A08), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_badge, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_badge, UI_COL_WARN, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_badge, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(s_badge, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_badge, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_badge, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *bl = lv_label_create(s_badge);
    lv_label_set_text(bl, "Locked");
    lv_obj_set_style_text_font(bl, UI_FONT_SMALL, LV_PART_MAIN);
    lv_obj_set_style_text_color(bl, UI_COL_WARN, LV_PART_MAIN);
    lv_obj_center(bl);

    s_hint = lv_obj_create(s_ov);
    lv_obj_set_size(s_hint, 300, 92);
    lv_obj_center(s_hint);
    lv_obj_set_style_bg_color(s_hint, lv_color_hex(0x0C1020), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_hint, LV_OPA_90, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_hint, UI_COL_ACCENT, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_hint, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(s_hint, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_hint, 12, LV_PART_MAIN);
    lv_obj_clear_flag(s_hint, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *t = lv_label_create(s_hint);
    lv_label_set_text(t, "Screen locked\nPress and hold to unlock");
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(t, UI_FONT_BODY, LV_PART_MAIN);
    lv_obj_set_style_text_color(t, UI_COL_TEXT, LV_PART_MAIN);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 0);
    s_bar = lv_bar_create(s_hint);
    lv_obj_set_size(s_bar, 260, 8);
    lv_obj_align(s_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_bar_set_range(s_bar, 0, 100);
    lv_obj_set_style_bg_color(s_bar, UI_COL_PANEL_BDR, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bar, UI_COL_ACCENT, LV_PART_INDICATOR);
    lv_obj_clear_flag(s_bar, LV_OBJ_FLAG_CLICKABLE);
    hint_show(1500);

    // Keep any open fault popup above the lock so it can still be dismissed.
    lv_obj_move_background(s_ov);
    VLOG("[UI] Screen locked\n");
}

void lock_release() {
    if (!s_ov) return;
    if (s_hide) { lv_timer_del(s_hide); s_hide = nullptr; }
    // The finger that unlocked is still down. Without this, LVGL re-targets the
    // press to whatever is under it once the overlay goes (slider, On toggle).
    lv_indev_t *in = lv_indev_get_act();
    if (!in) in = lv_indev_get_next(nullptr);
    if (in) lv_indev_wait_release(in);
    lv_obj_t *o = s_ov;
    s_ov = s_hint = s_bar = s_badge = nullptr;
    lv_obj_del_async(o);            // called from inside its own event handler
    VLOG("[UI] Screen unlocked\n");
}

bool lock_active() { return s_ov != nullptr; }
