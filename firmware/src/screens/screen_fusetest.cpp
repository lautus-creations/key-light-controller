// SPDX-FileCopyrightText: 2026 DrEvan
// SPDX-License-Identifier: GPL-3.0-or-later

/*
 * screen_fusetest.cpp - Menu > Fuse Cycle Test (bench tool)
 *
 * Start / Stop, live cycle count and rail reading. Leaving the page stops a
 * running test and restores normal panel outputs from saved state.
 */

#include "screens/screen_fusetest.h"
#include "fuse_test.h"
#include "ui_theme.h"
#include "save_manager.h"
#include <Arduino.h>
#include <cstdio>

static lv_obj_t   *s_status = nullptr;
static lv_obj_t   *s_detail = nullptr;
static lv_obj_t   *s_btn    = nullptr;
static lv_obj_t   *s_btnlbl = nullptr;
static lv_timer_t *s_timer  = nullptr;
static bool        s_touched_outputs = false;

static void refresh(lv_timer_t *) {
    const FuseTestStatus &st = fuse_test_status();
    char a[64], b[96];
    lv_color_t col = UI_COL_TEXT;
    switch (st.state) {
        case FT_IDLE:    snprintf(a, sizeof(a), "Ready");                                   col = UI_COL_TEXT;    break;
        case FT_ON:      snprintf(a, sizeof(a), "Cycle %u / %u   ON",  st.cycle, st.total); col = UI_COL_ACCENT;  break;
        case FT_OFF:     snprintf(a, sizeof(a), "Cycle %u / %u   off", st.cycle, st.total); col = UI_COL_ACCENT;  break;
        case FT_PASSED:  snprintf(a, sizeof(a), "PASSED  -  %u turn-ons", st.total);        col = UI_COL_GOOD;    break;
        case FT_FAILED:  snprintf(a, sizeof(a), "FAILED at cycle %u", st.cycle);            col = UI_COL_OFF_LIT; break;
        case FT_ABORTED: snprintf(a, sizeof(a), "Stopped at cycle %u", st.cycle);           col = UI_COL_WARN;    break;
    }
    if (st.cycle == 0)
        snprintf(b, sizeof(b), "Panel A, %u cycles: 1.5 s on / 5 s off (~%u min)",
                 st.total, (unsigned)((st.total * 6.5f) / 60 + 0.99f));
    else if (st.state == FT_FAILED)
        snprintf(b, sizeof(b), "Rail %.1f V, %.2f A: %s Leave page to restore.", st.last_rail_v, st.last_amps,
                 st.last_rail_v < FUSE_TEST_RAIL_OK_V ? "fuse likely open." : "no current: panel plugged in?");
    else
        snprintf(b, sizeof(b), "Last: %.1f V, %.2f A   lowest rail %.1f V%s", st.last_rail_v, st.last_amps,
                 st.min_rail_v > 90 ? 0.0f : st.min_rail_v,
                 fuse_test_running() ? "" : "   (leave page to restore)");
    lv_label_set_text(s_status, a);
    lv_obj_set_style_text_color(s_status, col, LV_PART_MAIN);
    lv_label_set_text(s_detail, b);
    lv_label_set_text(s_btnlbl, fuse_test_running() ? "Stop" : "Start");
    lv_obj_set_style_bg_color(s_btn, fuse_test_running() ? UI_COL_OFF_LIT : UI_COL_ACCENT, LV_PART_MAIN);
}

static void btn_cb(lv_event_t *) {
    if (fuse_test_running()) fuse_test_stop();
    else { fuse_test_start(); s_touched_outputs = true; }
    refresh(nullptr);
}

static void deleted_cb(lv_event_t *) {
    if (s_timer) { lv_timer_del(s_timer); s_timer = nullptr; }
    if (fuse_test_running()) fuse_test_stop();
    if (s_touched_outputs) { outputs_apply_all(); s_touched_outputs = false; }
    s_status = s_detail = s_btn = s_btnlbl = nullptr;
}

lv_obj_t *fusetest_page_create(lv_obj_t *parent, lv_event_cb_t back_cb) {
    lv_obj_t *v = lv_obj_create(parent);
    lv_obj_set_size(v, UI_W, UI_H);
    lv_obj_set_pos(v, 0, 0);
    lv_obj_set_style_bg_color(v, UI_COL_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(v, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(v, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(v, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(v, 0, LV_PART_MAIN);
    lv_obj_clear_flag(v, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(v, deleted_cb, LV_EVENT_DELETE, nullptr);

    lv_obj_t *bar = lv_obj_create(v);
    lv_obj_set_size(bar, UI_W, UI_BAR_H);
    lv_obj_set_pos(bar, 0, 0);
    ui_style_bar(bar);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
    lv_obj_t *t = lv_label_create(bar);
    lv_label_set_text(t, "Fuse Cycle Test");
    lv_obj_set_style_text_color(t, UI_COL_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(t, UI_FONT_LABEL, LV_PART_MAIN);
    lv_obj_align(t, LV_ALIGN_LEFT_MID, UI_SAFE, 0);
    lv_obj_t *back = lv_btn_create(bar);
    lv_obj_set_size(back, 90, UI_BAR_BTN_H);
    lv_obj_align(back, LV_ALIGN_RIGHT_MID, -UI_SAFE, 0);
    ui_style_outline_btn(back);
    ui_hit_pad(back, UI_HIT_PAD);
    lv_obj_set_style_pad_all(back, 0, LV_PART_MAIN);
    lv_obj_t *bl = lv_label_create(back);
    lv_label_set_text(bl, "< Back");
    lv_obj_set_style_text_color(bl, UI_COL_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(bl, UI_FONT_SMALL, LV_PART_MAIN);
    lv_obj_center(bl);
    lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, nullptr);

    int y = UI_BAR_H + 12;
    lv_obj_t *info = lv_label_create(v);
    lv_label_set_long_mode(info, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(info, UI_W - 2 * UI_SAFE);
    lv_label_set_text(info,
        "Turns Panel A on (100%, 4600K) and off like a real turn-on, so each cycle "
        "puts the capacitor-charging surge plus the LED load through the fuse. "
        "Stops if the rail reads dead or no current flows. Other panels keep running.");
    lv_obj_set_style_text_font(info, UI_FONT_SMALL, LV_PART_MAIN);
    lv_obj_set_style_text_color(info, UI_COL_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_pos(info, UI_SAFE, y);

    s_status = lv_label_create(v);
    lv_obj_set_style_text_font(s_status, UI_FONT_VALUE, LV_PART_MAIN);
    lv_obj_set_pos(s_status, UI_SAFE, y + 62);

    s_detail = lv_label_create(v);
    lv_label_set_long_mode(s_detail, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_detail, UI_W - 2 * UI_SAFE);
    lv_obj_set_style_text_font(s_detail, UI_FONT_BODY, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_detail, UI_COL_TEXT, LV_PART_MAIN);
    lv_obj_set_pos(s_detail, UI_SAFE, y + 94);

    s_btn = lv_btn_create(v);
    lv_obj_set_size(s_btn, 160, 44);
    lv_obj_align(s_btn, LV_ALIGN_BOTTOM_MID, 0, -16);
    lv_obj_set_style_radius(s_btn, 6, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_btn, 0, LV_PART_MAIN);
    ui_hit_pad(s_btn, UI_HIT_PAD);
    s_btnlbl = lv_label_create(s_btn);
    lv_obj_set_style_text_font(s_btnlbl, UI_FONT_VALUE, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_btnlbl, UI_COL_WHITE, LV_PART_MAIN);
    lv_obj_center(s_btnlbl);
    lv_obj_add_event_cb(s_btn, btn_cb, LV_EVENT_CLICKED, nullptr);

    s_touched_outputs = false;
    refresh(nullptr);
    if (s_timer) lv_timer_del(s_timer);
    s_timer = lv_timer_create(refresh, 250, nullptr);
    return v;
}
