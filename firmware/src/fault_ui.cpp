// SPDX-FileCopyrightText: 2026 DrEvan
// SPDX-License-Identifier: GPL-3.0-or-later

/*
 * fault_ui.cpp - Fault modal and main-screen status dot colour.
 */

#include "fault_ui.h"
#include "fault.h"
#include "app_state.h"
#include "ui_theme.h"
#include "screens/screen_manager.h"
#include "screens/screen_main.h"
#include "save_manager.h"
#include <Arduino.h>
#include <cstdio>

static lv_obj_t *s_modal       = nullptr;
static uint32_t  s_seen_gen    = 0;
static uint8_t   s_prev_trips  = 0;
static uint32_t  s_prev_trip_count = 0;
static bool      s_supply_note_shown = false;

static uint8_t trip_mask() {
    uint8_t m = 0;
    for (uint8_t p = 0; p < 3; p++) if (fault_tripped(p)) m |= 1u << p;
    return m;
}

static void close_modal() {
    if (s_modal) { lv_obj_del(s_modal); s_modal = nullptr; }
}

static void ok_cb(lv_event_t *e) { close_modal(); }

// A persistent cause re-trips the panel and the popup reappears.
void fault_ui_reset_all() {
    for (uint8_t p = 0; p < 3; p++) if (fault_tripped(p)) fault_reset(p);
    outputs_apply_all();
}

static void reset_cb(lv_event_t *e) {
    fault_ui_reset_all();
    close_modal();   // must be last: deletes the button that sent this event
}

static lv_obj_t *modal_btn(lv_obj_t *card, const char *text, int x_ofs, lv_event_cb_t cb) {
    lv_obj_t *btn = lv_btn_create(card);
    lv_obj_set_size(btn, 96, 38);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_RIGHT, x_ofs, 0);
    lv_obj_set_style_bg_color(btn, UI_COL_ACCENT_DIM, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn, UI_COL_ACCENT, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 6, LV_PART_MAIN);
    lv_obj_t *bl = lv_label_create(btn);
    lv_label_set_text(bl, text);
    lv_obj_set_style_text_font(bl, UI_FONT_LABEL, LV_PART_MAIN);
    lv_obj_set_style_text_color(bl, UI_COL_WHITE, LV_PART_MAIN);
    lv_obj_center(bl);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
    return btn;
}

static void show_modal(const char *title, const char *body, const char *hint, lv_color_t accent,
                       bool with_reset = false) {
    close_modal();

    // Backdrop: full screen, absorbs touches so nothing underneath fires.
    s_modal = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_modal, UI_W, UI_H);
    lv_obj_set_pos(s_modal, 0, 0);
    lv_obj_set_style_bg_color(s_modal, UI_COL_BLACK, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_modal, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_modal, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_modal, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_modal, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_modal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_modal, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *card = lv_obj_create(s_modal);
    lv_obj_set_size(card, 400, 220);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x12121E), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, accent, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(card, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 14, LV_PART_MAIN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *t = lv_label_create(card);
    lv_label_set_text(t, title);
    lv_obj_set_style_text_font(t, UI_FONT_VALUE, LV_PART_MAIN);
    lv_obj_set_style_text_color(t, accent, LV_PART_MAIN);
    lv_obj_set_pos(t, 0, 0);

    lv_obj_t *b = lv_label_create(card);
    lv_label_set_long_mode(b, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(b, 400 - 28);
    lv_label_set_text(b, body);
    lv_obj_set_style_text_font(b, UI_FONT_BODY, LV_PART_MAIN);
    lv_obj_set_style_text_color(b, UI_COL_TEXT, LV_PART_MAIN);
    lv_obj_set_pos(b, 0, 34);

    lv_obj_t *h = lv_label_create(card);
    lv_label_set_long_mode(h, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(h, with_reset ? (400 - 28 - 212) : (400 - 28 - 110));
    lv_label_set_text(h, hint);
    lv_obj_set_style_text_font(h, UI_FONT_SMALL, LV_PART_MAIN);
    lv_obj_set_style_text_color(h, UI_COL_TEXT_DIM, LV_PART_MAIN);
    lv_obj_align(h, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    modal_btn(card, "OK", 0, ok_cb);
    if (with_reset) modal_btn(card, "Reset", -106, reset_cb);
}

static void show_trip_modal(uint8_t mask) {
    char title[48], body[384], one[192];
    int n = 0, first = -1;
    for (uint8_t p = 0; p < 3; p++) if (mask & (1u << p)) { n++; if (first < 0) first = p; }

    if (n == 1) snprintf(title, sizeof(title), "%s shut off", g_state.name[first]);
    else        snprintf(title, sizeof(title), "%d panels shut off", n);

    size_t off = 0;
    body[0] = '\0';
    for (uint8_t p = 0; p < 3; p++) {
        if (!(mask & (1u << p))) continue;
        fault_describe(p, one, sizeof(one));
        int w = (n == 1) ? snprintf(body + off, sizeof(body) - off, "%s", one)
                         : snprintf(body + off, sizeof(body) - off, "%s%s: %s",
                                    off ? "\n" : "", g_state.name[p], one);
        if (w < 0 || (size_t)w >= sizeof(body) - off) break;
        off += w;
    }
    show_modal(title, body, "Fix the cause, then tap Reset (or turn the panel Off and On).",
               UI_COL_OFF_LIT, true);
}

static void poll_cb(lv_timer_t *t) {
    // Defer popups until the splash is gone; the pending generation is
    // handled on the first poll after.
    if (screen_manager_current() == SCR_SPLASH) return;

    uint32_t gen = fault_generation();
    if (gen == s_seen_gen) return;
    s_seen_gen = gen;

    uint8_t  mask  = trip_mask();
    uint32_t tc    = fault_trip_count();
    bool     added = (mask & ~s_prev_trips) || (tc != s_prev_trip_count && mask);
    s_prev_trip_count = tc;
    if (added) {
        show_trip_modal(mask);
    } else if (mask == 0 && s_prev_trips != 0) {
        close_modal();
    }
    s_prev_trips = mask;

    if (!mask && fault_no_supply() && !s_supply_note_shown) {
        s_supply_note_shown = true;
        show_modal("No voltage on panel outputs",
                   "The boot self-test found every panel rail dead. "
                   "Either the 24V input is off (USB power only) or the "
                   "panel fuses are not fitted.",
                   "Fit the fuses / connect 24V, then restart.",
                   UI_COL_WARN);
    }

    if (screen_manager_current() == SCR_MAIN) screen_main_refresh();
}

void fault_ui_init() {
    s_seen_gen = 0;   // force a first pass to show self-test results
    lv_timer_create(poll_cb, 250, nullptr);
}

lv_color_t fault_dot_color(int group, bool on) {
    switch (fault_group_level(group)) {
        case 2:  return UI_COL_OFF_LIT;
        case 1:  return UI_COL_WARN;
        default: return on ? UI_COL_GOOD : UI_COL_OFF;
    }
}
