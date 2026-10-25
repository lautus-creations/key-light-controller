// SPDX-FileCopyrightText: 2026 DrEvan
// SPDX-License-Identifier: GPL-3.0-or-later

/*
 * screen_settings.cpp - Menu > Output (PWM frequency, dimming curve)
 * Key Light Controller
 *
 *   [Output                                         < Back]
 *    PWM frequency       [20k][25k][30k][39k]
 *    Dimming curve       [Linear][Perceptual]
 */

#include "screens/screen_settings.h"
#include "ui_theme.h"
#include "app_state.h"
#include "save_manager.h"
#include "pwm.h"
#include <Arduino.h>
#include <cstring>


static const uint8_t PWM_KHZ[]   = { 20, 25, 30, 39 };   // 10-bit tops out at 39.06 kHz
static const char   *PWM_MAP[]   = { "20 kHz", "25 kHz", "30 kHz", "39 kHz", "" };
static const char   *CURVE_MAP[] = { "Linear", "Perceptual", "" };

static lv_obj_t *mk_text(lv_obj_t *parent, int x, int y, const lv_font_t *f, lv_color_t c, const char *t) {
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, t);
    lv_obj_set_style_text_font(l, f, LV_PART_MAIN);
    lv_obj_set_style_text_color(l, c, LV_PART_MAIN);
    lv_obj_set_pos(l, x, y);
    return l;
}

static void row_label(lv_obj_t *v, int y, const char *name, const char *hint) {
    mk_text(v, UI_SAFE, y + 4,  UI_FONT_BODY,  UI_COL_TEXT,     name);
    mk_text(v, UI_SAFE, y + 24, UI_FONT_SMALL, UI_COL_TEXT_DIM, hint);
}

static lv_obj_t *mk_segments(lv_obj_t *v, int x, int y, int w, const char **map, int checked) {
    lv_obj_t *m = lv_btnmatrix_create(v);
    lv_btnmatrix_set_map(m, map);
    lv_btnmatrix_set_btn_ctrl_all(m, LV_BTNMATRIX_CTRL_CHECKABLE);
    lv_btnmatrix_set_one_checked(m, true);
    if (checked >= 0) lv_btnmatrix_set_btn_ctrl(m, (uint16_t)checked, LV_BTNMATRIX_CTRL_CHECKED);
    lv_obj_set_size(m, w, 44);
    lv_obj_set_pos(m, x, y);
    lv_obj_set_style_bg_opa(m, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(m, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(m, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_gap(m, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(m, UI_COL_PANEL_BG, LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(m, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_border_color(m, UI_COL_PANEL_BDR, LV_PART_ITEMS);
    lv_obj_set_style_border_width(m, 1, LV_PART_ITEMS);
    lv_obj_set_style_radius(m, 6, LV_PART_ITEMS);
    lv_obj_set_style_text_font(m, UI_FONT_SMALL, LV_PART_ITEMS);
    lv_obj_set_style_text_color(m, UI_COL_TEXT_DIM, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(m, 0, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(m, UI_COL_ACCENT_DIM, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_color(m, UI_COL_ACCENT, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(m, UI_COL_WHITE, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(m, UI_COL_ACCENT, LV_PART_ITEMS | LV_STATE_PRESSED);
    return m;
}

static int idx_of(const uint8_t *arr, int n, uint8_t v) {
    for (int i = 0; i < n; i++) if (arr[i] == v) return i;
    return -1;
}

static void pwm_cb(lv_event_t *e) {
    uint16_t i = lv_btnmatrix_get_selected_btn(lv_event_get_target(e));
    if (i >= sizeof(PWM_KHZ)) return;
    lv_obj_t *m = lv_event_get_target(e);
    if (pwm_set_freq_khz(PWM_KHZ[i])) { g_state.pwm_khz = PWM_KHZ[i]; schedule_save(); return; }
    // Rejected: show the frequency that is actually running.
    int cur = idx_of(PWM_KHZ, sizeof(PWM_KHZ), (uint8_t)(pwm_freq_hz() / 1000));
    if (cur >= 0) lv_btnmatrix_set_btn_ctrl(m, (uint16_t)cur, LV_BTNMATRIX_CTRL_CHECKED);
}

static void curve_cb(lv_event_t *e) {
    uint16_t i = lv_btnmatrix_get_selected_btn(lv_event_get_target(e));
    if (i > 1) return;
    g_state.dim_curve = (uint8_t)i;
    pwm_set_curve(i == 1);
    outputs_apply_all();           // re-map every panel's brightness through the new curve
    schedule_save();
}

lv_obj_t *settings_page_create(lv_obj_t *parent, lv_event_cb_t back_cb) {
    lv_obj_t *v = lv_obj_create(parent);
    lv_obj_set_size(v, UI_W, UI_H);
    lv_obj_set_pos(v, 0, 0);
    lv_obj_set_style_bg_color(v, UI_COL_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(v, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(v, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(v, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(v, 0, LV_PART_MAIN);
    lv_obj_clear_flag(v, LV_OBJ_FLAG_SCROLLABLE);

    // Header
    lv_obj_t *bar = lv_obj_create(v);
    lv_obj_set_size(bar, UI_W, UI_BAR_H);
    lv_obj_set_pos(bar, 0, 0);
    ui_style_bar(bar);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
    lv_obj_t *title = lv_label_create(bar);
    lv_label_set_text(title, "Output");
    lv_obj_set_style_text_color(title, UI_COL_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(title, UI_FONT_LABEL, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, UI_SAFE, 0);
    lv_obj_t *back = lv_btn_create(bar);
    lv_obj_set_size(back, 90, UI_BAR_BTN_H);
    ui_hit_pad(back, UI_HIT_PAD);
    lv_obj_align(back, LV_ALIGN_RIGHT_MID, -UI_SAFE, 0);
    ui_style_outline_btn(back);
    lv_obj_set_style_pad_all(back, 0, LV_PART_MAIN);
    lv_obj_t *bl = lv_label_create(back);
    lv_label_set_text(bl, "< Back");
    lv_obj_set_style_text_color(bl, UI_COL_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(bl, UI_FONT_SMALL, LV_PART_MAIN);
    lv_obj_center(bl);
    lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, nullptr);

    const int X_CTL = 168;
    const int W_CTL = UI_W - UI_SAFE - X_CTL;
    const int ROW   = 70;
    int y = UI_BAR_H + 16;

    row_label(v, y, "PWM frequency", "Camera flicker tests");
    lv_obj_t *pm = mk_segments(v, X_CTL, y, W_CTL, PWM_MAP, idx_of(PWM_KHZ, sizeof(PWM_KHZ), g_state.pwm_khz));
    lv_obj_add_event_cb(pm, pwm_cb, LV_EVENT_VALUE_CHANGED, nullptr);
    y += ROW;

    row_label(v, y, "Dimming curve", "Brightness response");
    lv_obj_t *cm = mk_segments(v, X_CTL, y, W_CTL, CURVE_MAP, g_state.dim_curve ? 1 : 0);
    lv_obj_add_event_cb(cm, curve_cb, LV_EVENT_VALUE_CHANGED, nullptr);

    return v;
}
