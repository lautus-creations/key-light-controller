// SPDX-FileCopyrightText: 2026 DrEvan
// SPDX-License-Identifier: GPL-3.0-or-later

/*
 * screen_menu.cpp - Settings menu and its sub-pages
 * Key Light Controller
 *
 * Rows: Port Configuration, Edit Presets, Output, Diagnostics, About.
 * Port and preset names are edited in a full-screen overlay with the LVGL
 * keyboard: OK saves to g_state and NVS, Cancel discards.
 */

#include "screens/screen_menu.h"
#include "screens/screen_manager.h"
#include "ui_theme.h"
#include "app_state.h"
#include "storage.h"
#include "save_manager.h"
#include "version.h"
#include "pwm.h"
#include "screens/screen_diag.h"
#include "screens/screen_fusetest.h"
#include "screens/screen_settings.h"
#include "screens/screen_snake.h"
#include "sense_config.h"
#include "fault.h"
#include <math.h>
#include <Arduino.h>

typedef enum {
    SUB_NONE = 0,
    SUB_PORTS,
    SUB_PRESETS,
    SUB_ABOUT,
    SUB_DIAG,
    SUB_FUSETEST,
    SUB_DISPLAY,
} sub_page_t;

static lv_obj_t *s_scr       = nullptr;
static lv_obj_t *s_list_view = nullptr;
static lv_obj_t *s_sub_view  = nullptr;

static lv_obj_t *s_warn_ov = nullptr;   // all-ports-off warning overlay

static void show_list();
static lv_obj_t* make_sub_page(lv_obj_t *parent, sub_page_t sub);

static void show_sub(sub_page_t sub) {
    if (s_sub_view) { lv_obj_del(s_sub_view); s_sub_view = nullptr; }
    lv_obj_add_flag(s_list_view, LV_OBJ_FLAG_HIDDEN);
    s_sub_view = make_sub_page(s_scr, sub);
}
static void show_list() {
    if (s_sub_view) { lv_obj_del(s_sub_view); s_sub_view = nullptr; }
    lv_obj_clear_flag(s_list_view, LV_OBJ_FLAG_HIDDEN);
}
static void sub_back_cb(lv_event_t *e) { show_list(); }

// Diagnostics opened via the panel-detail shortcut returns to that panel.
static int s_diag_return_panel = -1;
static void diag_back_cb(lv_event_t *e) {
    if (s_diag_return_panel >= 0) screen_manager_goto_panel_prev(s_diag_return_panel);
    else                          show_list();
}

// ── All-ports-off warning dialog ──────────────────────────────
static void show_all_off_warning() {
    if (s_warn_ov) return;   // already showing

    s_warn_ov = lv_obj_create(s_scr);
    lv_obj_set_size(s_warn_ov, UI_W, UI_H);
    lv_obj_set_pos(s_warn_ov, 0, 0);
    lv_obj_set_style_bg_color(s_warn_ov, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_warn_ov, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_warn_ov, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_warn_ov, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_warn_ov, LV_OBJ_FLAG_SCROLLABLE);

    const int DW = 380, DH = 164;
    lv_obj_t *dlg = lv_obj_create(s_warn_ov);
    lv_obj_set_size(dlg, DW, DH);
    lv_obj_align(dlg, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(dlg, lv_color_hex(0x0C1020), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(dlg, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(dlg, UI_COL_WARN, LV_PART_MAIN);
    lv_obj_set_style_border_width(dlg, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(dlg, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_all(dlg, 14, LV_PART_MAIN);
    lv_obj_clear_flag(dlg, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_hd = lv_label_create(dlg);
    lv_label_set_text(lbl_hd, "WARNING");
    lv_obj_set_style_text_color(lbl_hd, UI_COL_WARN, LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl_hd, UI_FONT_LABEL, LV_PART_MAIN);
    lv_obj_align(lbl_hd, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *lbl_body = lv_label_create(dlg);
    lv_label_set_text(lbl_body, "All ports are disabled.");
    lv_obj_set_style_text_color(lbl_body, UI_COL_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl_body, UI_FONT_BODY, LV_PART_MAIN);
    lv_obj_set_style_text_align(lbl_body, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_size(lbl_body, DW - 28, LV_SIZE_CONTENT);
    lv_obj_align(lbl_body, LV_ALIGN_TOP_MID, 0, 28);

    const int BTN_W = 140, BTN_H = 36;

    lv_obj_t *btn_cancel = lv_btn_create(dlg);
    lv_obj_set_size(btn_cancel, BTN_W, BTN_H);
    lv_obj_align(btn_cancel, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(btn_cancel, UI_COL_PANEL_BDR, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn_cancel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_cancel, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(btn_cancel, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn_cancel, 0, LV_PART_MAIN);
    lv_obj_t *lbl_cancel = lv_label_create(btn_cancel);
    lv_label_set_text(lbl_cancel, "Cancel");
    lv_obj_set_style_text_color(lbl_cancel, UI_COL_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl_cancel, UI_FONT_BODY, LV_PART_MAIN);
    lv_obj_center(lbl_cancel);
    lv_obj_add_event_cb(btn_cancel, [](lv_event_t *e) {
        if (s_warn_ov) { lv_obj_del(s_warn_ov); s_warn_ov = nullptr; }
    }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *btn_cont = lv_btn_create(dlg);
    lv_obj_set_size(btn_cont, BTN_W, BTN_H);
    lv_obj_align(btn_cont, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(btn_cont, UI_COL_WARN, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn_cont, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_cont, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(btn_cont, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn_cont, 0, LV_PART_MAIN);
    lv_obj_t *lbl_cont = lv_label_create(btn_cont);
    lv_label_set_text(lbl_cont, "Continue");
    lv_obj_set_style_text_color(lbl_cont, UI_COL_WHITE, LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl_cont, UI_FONT_BODY, LV_PART_MAIN);
    lv_obj_center(lbl_cont);
    lv_obj_add_event_cb(btn_cont, [](lv_event_t *e) {
        if (s_warn_ov) { lv_obj_del(s_warn_ov); s_warn_ov = nullptr; }
        show_list();
    }, LV_EVENT_CLICKED, nullptr);
}

// Warn before leaving Port Configuration with every port disabled.
static void ports_back_cb(lv_event_t *e) {
    bool any_active = false;
    for (int i = 0; i < APP_MAX_PORTS; i++) {
        if (g_state.port_active[i]) { any_active = true; break; }
    }
    if (!any_active) {
        show_all_off_warning();
    } else {
        show_list();
    }
}

// ── Shared sub-page header (title + Back button) ──────────────
static lv_obj_t* make_sub_header(lv_obj_t *parent, const char *title,
                                  lv_event_cb_t back_fn = sub_back_cb) {
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, UI_W, UI_BAR_H);
    lv_obj_set_pos(bar, 0, 0);
    ui_style_bar(bar);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);

    lv_obj_t *lbl = lv_label_create(bar);
    lv_label_set_text(lbl, title);
    lv_obj_set_style_text_color(lbl, UI_COL_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl, UI_FONT_LABEL, LV_PART_MAIN);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, UI_SAFE, 0);

    lv_obj_t *btn = lv_btn_create(bar);
    lv_obj_set_size(btn, 90, UI_BAR_BTN_H);
    ui_hit_pad(btn, UI_HIT_PAD);
    lv_obj_align(btn, LV_ALIGN_RIGHT_MID, -UI_SAFE, 0);
    ui_style_outline_btn(btn);
    lv_obj_set_style_pad_all(btn, 0, LV_PART_MAIN);
    lv_obj_t *bl = lv_label_create(btn);
    lv_label_set_text(bl, "< Back");
    lv_obj_set_style_text_color(bl, UI_COL_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(bl, UI_FONT_SMALL, LV_PART_MAIN);
    lv_obj_center(bl);
    lv_obj_add_event_cb(btn, back_fn, LV_EVENT_CLICKED, nullptr);

    return bar;
}

// ── Name edit overlay ─────────────────────────────────────────
// Persistent state for the name-edit modal (one active at a time).
struct NameEditCtx {
    int        port_idx;
    lv_obj_t  *overlay;
    lv_obj_t  *ta;
    lv_obj_t  *name_lbl;   // port name label in the row, updated on save
};
static NameEditCtx s_ne;

static void name_kb_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_READY) {
        // OK key: save
        const char *txt = lv_textarea_get_text(s_ne.ta);
        if (txt) {
            lv_snprintf(g_state.name[s_ne.port_idx], APP_NAME_LEN, "%s", txt);
        }
        if (s_ne.name_lbl)
            lv_label_set_text(s_ne.name_lbl, g_state.name[s_ne.port_idx]);
        schedule_save();
    }
    // Both READY and CANCEL close the overlay
    if (s_ne.overlay) {
        lv_obj_del(s_ne.overlay);
        s_ne.overlay = nullptr;
        s_ne.ta      = nullptr;
    }
}

// ── Preset name edit overlay ──────────────────────────────────
// Mirrors the port name-edit overlay; edits g_state.preset[idx].name.
struct PresetNameEditCtx {
    int        preset_idx;
    lv_obj_t  *overlay;
    lv_obj_t  *ta;
    lv_obj_t  *name_lbl;   // name label on the preset card, updated on save
};
static PresetNameEditCtx s_pne;

static void preset_name_kb_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY) {
        const char *txt = lv_textarea_get_text(s_pne.ta);
        if (txt) {
            lv_snprintf(g_state.preset[s_pne.preset_idx].name,
                        sizeof(g_state.preset[s_pne.preset_idx].name), "%s", txt);
        }
        if (s_pne.name_lbl)
            lv_label_set_text(s_pne.name_lbl, g_state.preset[s_pne.preset_idx].name);
        schedule_save();
    }
    if (s_pne.overlay) {
        lv_obj_del(s_pne.overlay);
        s_pne.overlay = nullptr;
        s_pne.ta      = nullptr;
    }
}

static void open_preset_name_edit(lv_obj_t *parent_view, int preset_idx, lv_obj_t *name_lbl) {
    if (s_pne.overlay) { lv_obj_del(s_pne.overlay); s_pne.overlay = nullptr; }

    s_pne.preset_idx = preset_idx;
    s_pne.name_lbl   = name_lbl;

    lv_obj_t *ov = lv_obj_create(parent_view);
    lv_obj_set_size(ov, UI_W, UI_H);
    lv_obj_set_pos(ov, 0, 0);
    lv_obj_set_style_bg_color(ov, lv_color_hex(0x05060C), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ov, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(ov, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(ov, 0, LV_PART_MAIN);
    lv_obj_clear_flag(ov, LV_OBJ_FLAG_SCROLLABLE);
    s_pne.overlay = ov;

    lv_obj_t *bar = lv_obj_create(ov);
    lv_obj_set_size(bar, UI_W, UI_BAR_H);
    lv_obj_set_pos(bar, 0, 0);
    ui_style_bar(bar);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);

    char title[40];
    lv_snprintf(title, sizeof(title), "Preset %d - Edit Name", preset_idx + 1);
    lv_obj_t *lbl_t = lv_label_create(bar);
    lv_label_set_text(lbl_t, title);
    lv_obj_set_style_text_color(lbl_t, UI_COL_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl_t, UI_FONT_LABEL, LV_PART_MAIN);
    lv_obj_align(lbl_t, LV_ALIGN_LEFT_MID, UI_SAFE, 0);

    lv_obj_t *btn_x = lv_btn_create(bar);
    lv_obj_set_size(btn_x, 90, UI_BAR_BTN_H);
    ui_hit_pad(btn_x, UI_HIT_PAD);
    lv_obj_align(btn_x, LV_ALIGN_RIGHT_MID, -UI_SAFE, 0);
    ui_style_outline_btn(btn_x);
    lv_obj_set_style_pad_all(btn_x, 0, LV_PART_MAIN);
    lv_obj_t *bl = lv_label_create(btn_x);
    lv_label_set_text(bl, "Cancel");
    lv_obj_set_style_text_color(bl, UI_COL_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(bl, UI_FONT_SMALL, LV_PART_MAIN);
    lv_obj_center(bl);
    lv_obj_add_event_cb(btn_x, [](lv_event_t *e) {
        if (s_pne.overlay) { lv_obj_del(s_pne.overlay); s_pne.overlay = nullptr; s_pne.ta = nullptr; }
    }, LV_EVENT_CLICKED, nullptr);

    const int TA_Y = UI_BAR_H + 10;
    lv_obj_t *ta = lv_textarea_create(ov);
    lv_obj_set_size(ta, UI_W - 2 * UI_SAFE, 42);
    lv_obj_set_pos(ta, UI_SAFE, TA_Y);
    lv_textarea_set_text(ta, g_state.preset[preset_idx].name);
    lv_textarea_set_max_length(ta, (uint32_t)(sizeof(g_state.preset[preset_idx].name) - 1));
    lv_textarea_set_one_line(ta, true);
    lv_obj_set_style_bg_color(ta, lv_color_hex(0x0C1020), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(ta, UI_COL_ACCENT, LV_PART_MAIN);
    lv_obj_set_style_border_width(ta, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(ta, 4, LV_PART_MAIN);
    lv_obj_set_style_text_color(ta, UI_COL_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(ta, UI_FONT_BODY, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(ta, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(ta, 10, LV_PART_MAIN);
    s_pne.ta = ta;

    lv_obj_t *hint = lv_label_create(ov);
    lv_label_set_text(hint, "OK key = save  |  Cancel button = discard");
    lv_obj_set_style_text_color(hint, UI_COL_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(hint, UI_FONT_SMALL, LV_PART_MAIN);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_size(hint, UI_W, LV_SIZE_CONTENT);
    lv_obj_set_pos(hint, 0, TA_Y + 42 + 6);

    lv_obj_t *kb = lv_keyboard_create(ov);
    lv_obj_set_size(kb, UI_W, 178);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_UPPER);
    lv_keyboard_set_textarea(kb, ta);
    lv_obj_set_style_bg_color(kb, lv_color_hex(0x080B14), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(kb, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(kb, 0, LV_PART_MAIN);

    lv_obj_add_event_cb(kb, preset_name_kb_cb, LV_EVENT_READY,  nullptr);
    lv_obj_add_event_cb(kb, preset_name_kb_cb, LV_EVENT_CANCEL, nullptr);
}

static void open_name_edit(lv_obj_t *parent_view, int port_idx, lv_obj_t *name_lbl) {
    if (s_ne.overlay) { lv_obj_del(s_ne.overlay); s_ne.overlay = nullptr; }

    s_ne.port_idx = port_idx;
    s_ne.name_lbl = name_lbl;

    lv_obj_t *ov = lv_obj_create(parent_view);
    lv_obj_set_size(ov, UI_W, UI_H);
    lv_obj_set_pos(ov, 0, 0);
    lv_obj_set_style_bg_color(ov, lv_color_hex(0x05060C), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ov, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(ov, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(ov, 0, LV_PART_MAIN);
    lv_obj_clear_flag(ov, LV_OBJ_FLAG_SCROLLABLE);
    s_ne.overlay = ov;

    lv_obj_t *bar = lv_obj_create(ov);
    lv_obj_set_size(bar, UI_W, UI_BAR_H);
    lv_obj_set_pos(bar, 0, 0);
    ui_style_bar(bar);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);

    char title[32];
    lv_snprintf(title, sizeof(title), "Port %c - Edit Name", 'A' + port_idx);
    lv_obj_t *lbl_t = lv_label_create(bar);
    lv_label_set_text(lbl_t, title);
    lv_obj_set_style_text_color(lbl_t, UI_COL_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl_t, UI_FONT_LABEL, LV_PART_MAIN);
    lv_obj_align(lbl_t, LV_ALIGN_LEFT_MID, UI_SAFE, 0);

    // The keyboard's close key also cancels; this is the visible control.
    lv_obj_t *btn_x = lv_btn_create(bar);
    lv_obj_set_size(btn_x, 90, UI_BAR_BTN_H);
    ui_hit_pad(btn_x, UI_HIT_PAD);
    lv_obj_align(btn_x, LV_ALIGN_RIGHT_MID, -UI_SAFE, 0);
    ui_style_outline_btn(btn_x);
    lv_obj_set_style_pad_all(btn_x, 0, LV_PART_MAIN);
    lv_obj_t *bl = lv_label_create(btn_x);
    lv_label_set_text(bl, "Cancel");
    lv_obj_set_style_text_color(bl, UI_COL_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(bl, UI_FONT_SMALL, LV_PART_MAIN);
    lv_obj_center(bl);
    lv_obj_add_event_cb(btn_x, [](lv_event_t *e) {
        if (s_ne.overlay) { lv_obj_del(s_ne.overlay); s_ne.overlay = nullptr; s_ne.ta = nullptr; }
    }, LV_EVENT_CLICKED, nullptr);

    const int TA_Y = UI_BAR_H + 10;
    lv_obj_t *ta = lv_textarea_create(ov);
    lv_obj_set_size(ta, UI_W - 2 * UI_SAFE, 42);
    lv_obj_set_pos(ta, UI_SAFE, TA_Y);
    lv_textarea_set_text(ta, g_state.name[port_idx]);
    lv_textarea_set_max_length(ta, APP_NAME_LEN - 1);
    lv_textarea_set_one_line(ta, true);
    lv_obj_set_style_bg_color(ta, lv_color_hex(0x0C1020), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(ta, UI_COL_ACCENT, LV_PART_MAIN);
    lv_obj_set_style_border_width(ta, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(ta, 4, LV_PART_MAIN);
    lv_obj_set_style_text_color(ta, UI_COL_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(ta, UI_FONT_BODY, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(ta, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(ta, 10, LV_PART_MAIN);
    s_ne.ta = ta;

    lv_obj_t *hint = lv_label_create(ov);
    lv_label_set_text(hint, "OK key = save  |  Cancel button = discard");
    lv_obj_set_style_text_color(hint, UI_COL_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(hint, UI_FONT_SMALL, LV_PART_MAIN);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_size(hint, UI_W, LV_SIZE_CONTENT);
    lv_obj_set_pos(hint, 0, TA_Y + 42 + 6);

    lv_obj_t *kb = lv_keyboard_create(ov);
    lv_obj_set_size(kb, UI_W, 178);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_UPPER);
    lv_keyboard_set_textarea(kb, ta);

    lv_obj_set_style_bg_color(kb, lv_color_hex(0x080B14), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(kb, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(kb, 0, LV_PART_MAIN);

    lv_obj_add_event_cb(kb, name_kb_cb, LV_EVENT_READY,  nullptr);
    lv_obj_add_event_cb(kb, name_kb_cb, LV_EVENT_CANCEL, nullptr);
}

// ── Sub-page: Port Configuration ──────────────────────────────
// Static storage: callback user_data must outlive the sub-page.
struct PortEditCtx { int port; lv_obj_t *view; lv_obj_t *lbl; };
static PortEditCtx s_pec[APP_MAX_PORTS];

// ── Over-current trip limit (per port; auto, user can override) ──
static lv_obj_t *s_oc_btn_lbl[APP_MAX_PORTS];
static lv_obj_t *s_oc_ov  = nullptr;       // editor overlay
static lv_obj_t *s_oc_val = nullptr;
static lv_obj_t *s_oc_how = nullptr;
static int       s_oc_port = 0;
static lv_obj_t *s_oc_newp = nullptr;     // "New panel" (clear reference)
static lv_obj_t *s_oc_warn = nullptr;     // manual-override warning (must be acknowledged)
static bool      s_oc_ack  = false;       // acknowledged for this editor session
static int       s_oc_pending = 0;        // step to apply once acknowledged
static void oc_step(int dir);
static lv_obj_t *oc_btn(lv_obj_t *parent, const char *txt, int w, bool filled);

static void oc_fmt_limit(char *b, size_t n, int p) {
    float w = fault_oc_limit_a(p, 0), c = fault_oc_limit_a(p, 1);
    if (fabsf(w - c) < 0.005f) lv_snprintf(b, n, "%d.%02d A", (int)w, (int)(w * 100 + 0.5f) % 100);
    else lv_snprintf(b, n, "%d.%02d / %d.%02d A", (int)w, (int)(w * 100 + 0.5f) % 100,
                     (int)c, (int)(c * 100 + 0.5f) % 100);
}

static void oc_row_refresh(int p) {
    if (!s_oc_btn_lbl[p]) return;
    char v[24], b[48];
    oc_fmt_limit(v, sizeof(v), p);
    lv_snprintf(b, sizeof(b), "Trip limit\n%s %s", v, g_state.oc_ma[p] ? "MANUAL" : "auto");
    lv_label_set_text(s_oc_btn_lbl[p], b);
    lv_obj_set_style_text_color(s_oc_btn_lbl[p], g_state.oc_ma[p] ? UI_COL_WARN : UI_COL_TEXT, LV_PART_MAIN);
}

static void oc_editor_refresh() {
    if (!s_oc_val) return;
    int p = s_oc_port;
    char v[24], b[120];
    oc_fmt_limit(v, sizeof(v), p);
    lv_label_set_text(s_oc_val, v);
    if (g_state.oc_ma[p])
        lv_snprintf(b, sizeof(b), "MANUAL: a fixed limit replaces the calculated one. Reset to auto restores it.");
    else if (g_state.ref_ma[p][0] || g_state.ref_ma[p][1])
        lv_snprintf(b, sizeof(b), "Automatic: 2x this panel's Panel Check reference.");
    else
        lv_snprintf(b, sizeof(b), "Automatic: default (no Panel Check reference saved yet).");
    lv_label_set_text(s_oc_how, b);
    lv_obj_set_style_text_color(s_oc_how, g_state.oc_ma[p] ? UI_COL_WARN : UI_COL_TEXT_DIM, LV_PART_MAIN);
    if (s_oc_newp) {
        if (g_state.ref_ma[p][0] || g_state.ref_ma[p][1]) lv_obj_clear_state(s_oc_newp, LV_STATE_DISABLED);
        else                                              lv_obj_add_state(s_oc_newp, LV_STATE_DISABLED);
    }
    oc_row_refresh(p);
}

// Shown the first time +/- is used on a port that is still automatic.
static void oc_warn_close() {
    if (s_oc_warn) { lv_obj_del_async(s_oc_warn); s_oc_warn = nullptr; }
}

static void oc_show_warning(int dir) {
    if (s_oc_warn || !s_oc_ov) return;
    s_oc_pending = dir;
    s_oc_warn = lv_obj_create(s_oc_ov);
    lv_obj_set_size(s_oc_warn, 420, 250);
    lv_obj_center(s_oc_warn);
    lv_obj_set_style_bg_color(s_oc_warn, lv_color_hex(0x1A1006), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_oc_warn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_oc_warn, UI_COL_WARN, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_oc_warn, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(s_oc_warn, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_oc_warn, 14, LV_PART_MAIN);
    lv_obj_clear_flag(s_oc_warn, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *hd = lv_label_create(s_oc_warn);
    lv_label_set_text(hd, "WARNING: manual trip limit");
    lv_obj_set_style_text_color(hd, UI_COL_WARN, LV_PART_MAIN);
    lv_obj_set_style_text_font(hd, UI_FONT_LABEL, LV_PART_MAIN);
    lv_obj_align(hd, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *body = lv_label_create(s_oc_warn);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(body, 388);
    lv_label_set_text(body,
        "This replaces the calculated over-current limit for this port with a "
        "fixed value that will not follow the panel's reference.\n"
        "Set too high, a partial short or damaged panel can overheat wiring, "
        "connectors or the panel before the firmware shuts it off; only the fuse "
        "is left. Set too low, the panel shuts off in normal use.\n"
        "Only change it if you know the panel's rated current and the fuse fitted.");
    lv_obj_set_style_text_color(body, UI_COL_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(body, UI_FONT_SMALL, LV_PART_MAIN);
    lv_obj_align(body, LV_ALIGN_TOP_LEFT, 0, 28);

    lv_obj_t *cancel = oc_btn(s_oc_warn, "Cancel", 150, false);
    lv_obj_align(cancel, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_add_event_cb(cancel, [](lv_event_t *e) { oc_warn_close(); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *ok = oc_btn(s_oc_warn, "I understand", 180, false);
    lv_obj_set_style_bg_color(ok, UI_COL_WARN, LV_PART_MAIN);
    lv_obj_align(ok, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_add_event_cb(ok, [](lv_event_t *e) {
        s_oc_ack = true;
        oc_warn_close();
        oc_step(s_oc_pending);
    }, LV_EVENT_CLICKED, nullptr);
}

static void oc_step(int dir) {
    int p = s_oc_port;
    if (!g_state.oc_ma[p] && !s_oc_ack) { oc_show_warning(dir); return; }
    int ma = g_state.oc_ma[p];
    if (!ma) {   // start from the current automatic value, rounded to the step
        float a = fault_oc_limit_a(p, 0);
        float c = fault_oc_limit_a(p, 1);
        ma = (int)((a > c ? a : c) * 1000.0f + 0.5f);
        ma = (ma + OC_MANUAL_STEP_MA / 2) / OC_MANUAL_STEP_MA * OC_MANUAL_STEP_MA;
    }
    ma += dir * OC_MANUAL_STEP_MA;
    if (ma < OC_MANUAL_MIN_MA) ma = OC_MANUAL_MIN_MA;
    if (ma > OC_MANUAL_MAX_MA) ma = OC_MANUAL_MAX_MA;
    g_state.oc_ma[p] = (uint16_t)ma;
    schedule_save();
    oc_editor_refresh();
}

static lv_obj_t *oc_btn(lv_obj_t *parent, const char *txt, int w, bool filled) {
    lv_obj_t *b = lv_btn_create(parent);
    lv_obj_set_size(b, w, 40);
    lv_obj_set_style_bg_color(b, filled ? UI_COL_ACCENT : UI_COL_PANEL_BDR, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(b, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(b, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_all(b, 0, LV_PART_MAIN);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_color(l, UI_COL_WHITE, LV_PART_MAIN);
    lv_obj_set_style_text_font(l, UI_FONT_LABEL, LV_PART_MAIN);
    lv_obj_center(l);
    return b;
}

static void oc_close() {
    if (s_oc_ov) { lv_obj_del_async(s_oc_ov); s_oc_ov = nullptr; }
    s_oc_val = s_oc_how = nullptr;
    s_oc_warn = nullptr;               // child of the overlay, deleted with it
    s_oc_newp = nullptr;
}

static void open_oc_editor(lv_obj_t *view, int p) {
    if (s_oc_ov) return;
    s_oc_port = p;
    s_oc_ack  = false;                 // warn again each time the editor is opened
    s_oc_ov = lv_obj_create(view);
    lv_obj_set_size(s_oc_ov, UI_W, UI_H);
    lv_obj_set_pos(s_oc_ov, 0, 0);
    lv_obj_set_style_bg_color(s_oc_ov, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_oc_ov, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_oc_ov, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_oc_ov, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_oc_ov, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *dlg = lv_obj_create(s_oc_ov);
    lv_obj_set_size(dlg, 400, 252);
    lv_obj_center(dlg);
    lv_obj_set_style_bg_color(dlg, lv_color_hex(0x0C1020), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(dlg, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(dlg, UI_COL_ACCENT, LV_PART_MAIN);
    lv_obj_set_style_border_width(dlg, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(dlg, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_all(dlg, 14, LV_PART_MAIN);
    lv_obj_clear_flag(dlg, LV_OBJ_FLAG_SCROLLABLE);

    char t[48]; lv_snprintf(t, sizeof(t), "%s: over-current trip", g_state.name[p]);
    lv_obj_t *hd = lv_label_create(dlg);
    lv_label_set_text(hd, t);
    lv_obj_set_style_text_color(hd, UI_COL_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(hd, UI_FONT_LABEL, LV_PART_MAIN);
    lv_obj_align(hd, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *minus = oc_btn(dlg, "-", 64, false);
    lv_obj_align(minus, LV_ALIGN_TOP_LEFT, 0, 36);
    lv_obj_add_event_cb(minus, [](lv_event_t *e) { oc_step(-1); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *plus = oc_btn(dlg, "+", 64, true);
    lv_obj_align(plus, LV_ALIGN_TOP_RIGHT, 0, 36);
    lv_obj_add_event_cb(plus, [](lv_event_t *e) { oc_step(+1); }, LV_EVENT_CLICKED, nullptr);

    s_oc_val = lv_label_create(dlg);
    lv_obj_set_style_text_color(s_oc_val, UI_COL_WHITE, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_oc_val, UI_FONT_VALUE, LV_PART_MAIN);
    lv_obj_align(s_oc_val, LV_ALIGN_TOP_MID, 0, 46);

    s_oc_how = lv_label_create(dlg);
    lv_label_set_long_mode(s_oc_how, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_oc_how, 372);
    lv_obj_set_style_text_color(s_oc_how, UI_COL_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_oc_how, UI_FONT_SMALL, LV_PART_MAIN);
    lv_obj_align(s_oc_how, LV_ALIGN_TOP_LEFT, 0, 90);

    lv_obj_t *note = lv_label_create(dlg);
    lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(note, 372);
    lv_label_set_text(note, "Per channel. Changed the panel on this port? Tap New panel, then run "
                            "Diagnostics > Check and Save reference.");
    lv_obj_set_style_text_color(note, UI_COL_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(note, UI_FONT_SMALL, LV_PART_MAIN);
    lv_obj_align(note, LV_ALIGN_TOP_LEFT, 0, 126);

    lv_obj_t *autob = oc_btn(dlg, "Reset to auto", 136, false);
    lv_obj_align(autob, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    // Swapping in a different panel: forget the old Panel Check reference so the
    // automatic limit falls back to the default until a new reference is saved.
    s_oc_newp = oc_btn(dlg, "New panel", 120, false);
    lv_obj_align(s_oc_newp, LV_ALIGN_BOTTOM_LEFT, 136 + 8, 0);
    lv_obj_set_style_bg_color(s_oc_newp, UI_COL_PANEL_BG, LV_PART_MAIN | LV_STATE_DISABLED);
    lv_obj_add_event_cb(s_oc_newp, [](lv_event_t *e) {
        g_state.ref_ma[s_oc_port][0] = g_state.ref_ma[s_oc_port][1] = 0;
        schedule_save();
        oc_editor_refresh();
    }, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(autob, [](lv_event_t *e) {
        g_state.oc_ma[s_oc_port] = 0;
        schedule_save();
        oc_editor_refresh();
    }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *done = oc_btn(dlg, "Done", 100, true);
    lv_obj_align(done, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_add_event_cb(done, [](lv_event_t *e) { oc_close(); }, LV_EVENT_CLICKED, nullptr);

    oc_editor_refresh();
}

static lv_obj_t* sub_ports(lv_obj_t *parent) {
    lv_obj_t *v = lv_obj_create(parent);
    lv_obj_set_size(v, UI_W, UI_H);
    lv_obj_set_pos(v, 0, 0);
    lv_obj_set_style_bg_color(v, UI_COL_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(v, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(v, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(v, 0, LV_PART_MAIN);
    lv_obj_clear_flag(v, LV_OBJ_FLAG_SCROLLABLE);
    make_sub_header(v, "Port Configuration", ports_back_cb);

    static const char *port_letters[3] = {"A", "B", "C"};
    int y     = UI_BAR_H + 10;
    int row_h = 68;

    for (int p = 0; p < APP_MAX_PORTS; p++) {
        lv_obj_t *row = lv_obj_create(v);
        lv_obj_set_size(row, UI_W - 2 * UI_SAFE, row_h);
        lv_obj_set_pos(row, UI_SAFE, y + p * (row_h + 6));
        ui_style_card(row);
        lv_obj_set_style_pad_all(row, 8, LV_PART_MAIN);

        char badge[8]; lv_snprintf(badge, sizeof(badge), "Port %s", port_letters[p]);
        lv_obj_t *lbl_port = lv_label_create(row);
        lv_label_set_text(lbl_port, badge);
        lv_obj_set_style_text_color(lbl_port, UI_COL_TEXT_DIM, LV_PART_MAIN);
        lv_obj_set_style_text_font(lbl_port, UI_FONT_SMALL, LV_PART_MAIN);
        lv_obj_align(lbl_port, LV_ALIGN_TOP_LEFT, 0, 0);

        // Panel name, truncated to clear the trip-limit button
        lv_obj_t *lbl_nm = lv_label_create(row);
        lv_label_set_text(lbl_nm, g_state.name[p]);
        lv_label_set_long_mode(lbl_nm, LV_LABEL_LONG_DOT);
        lv_obj_set_width(lbl_nm, 150);
        lv_obj_set_style_text_color(lbl_nm, UI_COL_TEXT, LV_PART_MAIN);
        lv_obj_set_style_text_font(lbl_nm, UI_FONT_BODY, LV_PART_MAIN);
        lv_obj_align(lbl_nm, LV_ALIGN_BOTTOM_LEFT, 0, 0);

        // Over-current trip limit; opens the editor
        lv_obj_t *btn_oc = lv_btn_create(row);
        lv_obj_set_size(btn_oc, 132, 46);
        ui_hit_pad(btn_oc, 4);
        lv_obj_align(btn_oc, LV_ALIGN_LEFT_MID, 160, 0);
        ui_style_outline_btn(btn_oc);
        lv_obj_set_style_pad_all(btn_oc, 0, LV_PART_MAIN);
        s_oc_btn_lbl[p] = lv_label_create(btn_oc);
        lv_obj_set_style_text_color(s_oc_btn_lbl[p], UI_COL_TEXT, LV_PART_MAIN);
        lv_obj_set_style_text_font(s_oc_btn_lbl[p], UI_FONT_SMALL, LV_PART_MAIN);
        lv_obj_set_style_text_align(s_oc_btn_lbl[p], LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_center(s_oc_btn_lbl[p]);
        oc_row_refresh(p);
        lv_obj_add_event_cb(btn_oc, [](lv_event_t *e) {
            PortEditCtx *ctx = (PortEditCtx*)lv_event_get_user_data(e);
            open_oc_editor(ctx->view, ctx->port);
        }, LV_EVENT_CLICKED, &s_pec[p]);

        lv_obj_t *btn_edit = lv_btn_create(row);
        lv_obj_set_size(btn_edit, 60, 34);
        ui_hit_pad(btn_edit, 4);
        lv_obj_align(btn_edit, LV_ALIGN_RIGHT_MID, 0, 0);

        lv_obj_t *sw = lv_switch_create(row);
        lv_obj_set_size(sw, 48, 24);
        lv_obj_align(sw, LV_ALIGN_RIGHT_MID, -(52 + 10), 0);  // left of the Edit button
        if (g_state.port_active[p]) lv_obj_add_state(sw, LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(sw, UI_COL_DIM,    LV_PART_MAIN);
        lv_obj_set_style_bg_color(sw, UI_COL_ACCENT, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(sw, UI_COL_WHITE,  LV_PART_KNOB);
        lv_obj_add_event_cb(sw, [](lv_event_t *e) {
            int idx = (int)(intptr_t)lv_event_get_user_data(e);
            bool active = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
            g_state.port_active[idx] = active;
            // Re-drive all ports from state. A deactivated port is cut at once;
            // an activated one lights only if system_on && panel_on.
            outputs_apply_all();
            groups_separate_all();
            schedule_save();
        }, LV_EVENT_VALUE_CHANGED, (void*)(intptr_t)p);
        lv_obj_set_style_bg_color(btn_edit, UI_COL_ACCENT_DIM, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(btn_edit, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(btn_edit, UI_COL_ACCENT, LV_PART_MAIN);
        lv_obj_set_style_border_width(btn_edit, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(btn_edit, 3, LV_PART_MAIN);
        lv_obj_set_style_pad_all(btn_edit, 0, LV_PART_MAIN);
        lv_obj_set_style_bg_color(btn_edit, UI_COL_ACCENT,
                                  LV_STATE_PRESSED | LV_PART_MAIN);
        lv_obj_t *be_lbl = lv_label_create(btn_edit);
        lv_label_set_text(be_lbl, "Edit");
        lv_obj_set_style_text_color(be_lbl, UI_COL_WHITE, LV_PART_MAIN);
        lv_obj_set_style_text_font(be_lbl, UI_FONT_SMALL, LV_PART_MAIN);
        lv_obj_center(be_lbl);

        s_pec[p] = { p, v, lbl_nm };
        lv_obj_add_event_cb(btn_edit, [](lv_event_t *e) {
            PortEditCtx *ctx = (PortEditCtx*)lv_event_get_user_data(e);
            open_name_edit(ctx->view, ctx->port, ctx->lbl);
        }, LV_EVENT_CLICKED, &s_pec[p]);
    }
    return v;
}

// ── Sub-page: Presets ─────────────────────────────────────────
// Context for preset Edit Name button callbacks
struct PresetEditCtx { int preset; lv_obj_t *view; lv_obj_t *lbl; };
static PresetEditCtx s_prec[APP_PRESET_CNT];
static lv_obj_t *s_preset_cct_lbl[APP_PRESET_CNT];

static void preset_cct_label_refresh(int idx) {
    if (idx < 0 || idx >= APP_PRESET_CNT || !s_preset_cct_lbl[idx]) return;
    char b[12]; lv_snprintf(b, sizeof(b), "%dK", g_state.preset[idx].cct);
    lv_label_set_text(s_preset_cct_lbl[idx], b);
}

static lv_obj_t* sub_presets(lv_obj_t *parent) {
    lv_obj_t *v = lv_obj_create(parent);
    lv_obj_set_size(v, UI_W, UI_H);
    lv_obj_set_pos(v, 0, 0);
    lv_obj_set_style_bg_color(v, UI_COL_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(v, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(v, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(v, 0, LV_PART_MAIN);
    lv_obj_clear_flag(v, LV_OBJ_FLAG_SCROLLABLE);
    make_sub_header(v, "Edit Presets");

    int y   = UI_BAR_H + 10;
    int cw  = (UI_W - 2 * UI_SAFE - 2 * 6) / APP_PRESET_CNT;

    for (int i = 0; i < APP_PRESET_CNT; i++) {
        int x = UI_SAFE + i * (cw + 6);

        lv_obj_t *card = lv_obj_create(v);
        lv_obj_set_size(card, cw, UI_H - y - UI_BAR_H - 8);
        lv_obj_set_pos(card, x, y);
        ui_style_card(card);
        lv_obj_set_style_pad_all(card, 6, LV_PART_MAIN);

        lv_obj_t *lbl_nm = lv_label_create(card);
        lv_label_set_text(lbl_nm, g_state.preset[i].name);
        lv_obj_set_style_text_color(lbl_nm, UI_COL_ACCENT, LV_PART_MAIN);
        lv_obj_set_style_text_font(lbl_nm, UI_FONT_SMALL, LV_PART_MAIN);
        lv_obj_align(lbl_nm, LV_ALIGN_TOP_MID, 0, 0);

        // Presets set CCT only; brightness is left alone.
        int bw = (cw - 12 - 4) / 2;
        lv_obj_t *lbl_cv = lv_label_create(card);
        char buf[16]; lv_snprintf(buf, sizeof(buf), "%dK", g_state.preset[i].cct);
        lv_label_set_text(lbl_cv, buf);
        lv_obj_set_style_text_color(lbl_cv, UI_COL_WHITE, LV_PART_MAIN);
        lv_obj_set_style_text_font(lbl_cv, UI_FONT_VALUE, LV_PART_MAIN);
        lv_obj_align(lbl_cv, LV_ALIGN_TOP_MID, 0, 24);
        s_preset_cct_lbl[i] = lbl_cv;

        lv_obj_t *cm = lv_btn_create(card);
        lv_obj_set_size(cm, bw, 32);
        lv_obj_align(cm, LV_ALIGN_TOP_LEFT, 0, 58);
        lv_obj_set_style_bg_color(cm, UI_COL_PANEL_BDR, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(cm, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(cm, 3, LV_PART_MAIN);
        lv_obj_set_style_border_width(cm, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(cm, 0, LV_PART_MAIN);
        lv_obj_t *cml = lv_label_create(cm); lv_label_set_text(cml, "-");
        lv_obj_set_style_text_color(cml, UI_COL_WHITE, LV_PART_MAIN);
        lv_obj_set_style_text_font(cml, UI_FONT_BODY, LV_PART_MAIN);
        lv_obj_center(cml);
        lv_obj_add_event_cb(cm, [](lv_event_t *e) {
            int idx = (int)(intptr_t)lv_event_get_user_data(e);
            if (g_state.preset[idx].cct > 2700) g_state.preset[idx].cct -= 100;
            preset_cct_label_refresh(idx);
            schedule_save();
        }, LV_EVENT_CLICKED, (void*)(intptr_t)i);

        lv_obj_t *cp = lv_btn_create(card);
        lv_obj_set_size(cp, bw, 32);
        lv_obj_align(cp, LV_ALIGN_TOP_RIGHT, 0, 58);
        lv_obj_set_style_bg_color(cp, UI_COL_ACCENT, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(cp, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(cp, 3, LV_PART_MAIN);
        lv_obj_set_style_border_width(cp, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(cp, 0, LV_PART_MAIN);
        lv_obj_t *cpl = lv_label_create(cp); lv_label_set_text(cpl, "+");
        lv_obj_set_style_text_color(cpl, UI_COL_WHITE, LV_PART_MAIN);
        lv_obj_set_style_text_font(cpl, UI_FONT_BODY, LV_PART_MAIN);
        lv_obj_center(cpl);
        lv_obj_add_event_cb(cp, [](lv_event_t *e) {
            int idx = (int)(intptr_t)lv_event_get_user_data(e);
            if (g_state.preset[idx].cct < 6500) g_state.preset[idx].cct += 100;
            preset_cct_label_refresh(idx);
            schedule_save();
        }, LV_EVENT_CLICKED, (void*)(intptr_t)i);

        lv_obj_t *btn_nm = lv_btn_create(card);
        lv_obj_set_size(btn_nm, cw - 12 - 8, 26);
        lv_obj_align(btn_nm, LV_ALIGN_BOTTOM_MID, 0, -4);
        lv_obj_set_style_bg_color(btn_nm, UI_COL_ACCENT_DIM, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(btn_nm, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(btn_nm, UI_COL_ACCENT, LV_PART_MAIN);
        lv_obj_set_style_border_width(btn_nm, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(btn_nm, 3, LV_PART_MAIN);
        lv_obj_set_style_pad_all(btn_nm, 0, LV_PART_MAIN);
        lv_obj_set_style_bg_color(btn_nm, UI_COL_ACCENT,
                                  LV_STATE_PRESSED | LV_PART_MAIN);
        lv_obj_t *nm_lbl_btn = lv_label_create(btn_nm);
        lv_label_set_text(nm_lbl_btn, "Edit Name");
        lv_obj_set_style_text_color(nm_lbl_btn, UI_COL_WHITE, LV_PART_MAIN);
        lv_obj_set_style_text_font(nm_lbl_btn, UI_FONT_SMALL, LV_PART_MAIN);
        lv_obj_center(nm_lbl_btn);

        s_prec[i] = { i, v, lbl_nm };
        lv_obj_add_event_cb(btn_nm, [](lv_event_t *e) {
            PresetEditCtx *ctx = (PresetEditCtx*)lv_event_get_user_data(e);
            open_preset_name_edit(ctx->view, ctx->preset, ctx->lbl);
        }, LV_EVENT_CLICKED, &s_prec[i]);
    }
    return v;
}

// ── Sub-page: About ───────────────────────────────────────────
static lv_obj_t* sub_about(lv_obj_t *parent) {
    lv_obj_t *v = lv_obj_create(parent);
    lv_obj_set_size(v, UI_W, UI_H);
    lv_obj_set_pos(v, 0, 0);
    lv_obj_set_style_bg_color(v, UI_COL_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(v, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(v, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(v, 0, LV_PART_MAIN);
    lv_obj_clear_flag(v, LV_OBJ_FLAG_SCROLLABLE);
    make_sub_header(v, "About");

    int cy = UI_BAR_H + 18;

    lv_obj_t *lbl_prod = lv_label_create(v);
    lv_label_set_text(lbl_prod, "Key Light Controller");
    lv_obj_set_style_text_color(lbl_prod, UI_COL_ACCENT, LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl_prod, UI_FONT_LABEL, LV_PART_MAIN);
    lv_obj_set_style_text_align(lbl_prod, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_size(lbl_prod, UI_W, LV_SIZE_CONTENT);
    lv_obj_set_pos(lbl_prod, 0, cy);
    lv_obj_add_flag(lbl_prod, LV_OBJ_FLAG_CLICKABLE);       // long-press opens Snake
    lv_obj_add_event_cb(lbl_prod, [](lv_event_t *e) { snake_open(s_scr); },
                        LV_EVENT_LONG_PRESSED, nullptr);
    cy += 26;

    lv_obj_t *lbl_brand = lv_label_create(v);
    lv_label_set_text(lbl_brand, "Designed by Lautus Creations");
    lv_obj_set_style_text_color(lbl_brand, UI_COL_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl_brand, UI_FONT_BODY, LV_PART_MAIN);
    lv_obj_set_style_text_align(lbl_brand, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_size(lbl_brand, UI_W, LV_SIZE_CONTENT);
    lv_obj_set_pos(lbl_brand, 0, cy);
    cy += 28;

    lv_obj_t *sep = lv_obj_create(v);
    lv_obj_set_size(sep, UI_W - 6 * UI_SAFE, 1);
    lv_obj_set_pos(sep, 3 * UI_SAFE, cy);
    lv_obj_set_style_bg_color(sep, UI_COL_PANEL_BDR, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(sep, 0, LV_PART_MAIN);
    cy += 14;

    // Info rows: label right-aligned in the left column, value in the right
    char ver_str[20];
    lv_snprintf(ver_str, sizeof(ver_str), "v%s", FW_VERSION);
    char pwm_str[24];
    lv_snprintf(pwm_str, sizeof(pwm_str), "%lu kHz / 10-bit", (unsigned long)(pwm_freq_hz() / 1000));

    struct InfoRow { const char *label; const char *value; bool dim_value; };
    InfoRow rows[] = {
        { "Firmware",  ver_str,              false },
        { "PWM",       pwm_str,              false },
        { "Platform",  "ESP32-S3-N16R8",     false },
        { "Build",     FW_BUILD,             false },
        { "Runtime",   "LVGL 8.4 / TFT_eSPI", true },
    };

    const int COL_LBL_X = UI_SAFE * 2;          // left column x (right-aligned text)
    const int COL_LBL_W = UI_W / 2 - UI_SAFE - 12;   // label column width (12px gap to values)
    const int COL_VAL_X = UI_W / 2 + UI_SAFE;   // right column x
    const int ROW_H     = 22;

    for (auto &r : rows) {
        lv_obj_t *lk = lv_label_create(v);
        lv_label_set_text(lk, r.label);
        lv_obj_set_style_text_color(lk, UI_COL_TEXT_DIM, LV_PART_MAIN);
        lv_obj_set_style_text_font(lk, UI_FONT_SMALL, LV_PART_MAIN);
        lv_obj_set_style_text_align(lk, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
        lv_label_set_long_mode(lk, LV_LABEL_LONG_CLIP);
        lv_obj_set_size(lk, COL_LBL_W, ROW_H);
        lv_obj_set_pos(lk, COL_LBL_X, cy);

        lv_obj_t *lval = lv_label_create(v);
        lv_label_set_text(lval, r.value);
        lv_obj_set_style_text_color(lval,
            r.dim_value ? UI_COL_TEXT_DIM : UI_COL_TEXT, LV_PART_MAIN);
        lv_obj_set_style_text_font(lval, UI_FONT_BODY, LV_PART_MAIN);
        lv_obj_set_pos(lval, COL_VAL_X, cy);

        cy += ROW_H;
    }

    return v;
}

static lv_obj_t* make_sub_page(lv_obj_t *parent, sub_page_t sub) {
    switch (sub) {
        case SUB_PORTS:   return sub_ports(parent);
        case SUB_PRESETS: return sub_presets(parent);
        case SUB_ABOUT:   return sub_about(parent);
        case SUB_DIAG:    return diag_page_create(parent, diag_back_cb);
        case SUB_DISPLAY: return settings_page_create(parent, sub_back_cb);
#if ENABLE_FUSE_CYCLE_TEST
        case SUB_FUSETEST: return fusetest_page_create(parent, sub_back_cb);
#endif
        default: return nullptr;
    }
}

static void row_cb(lv_event_t *e) {
    show_sub((sub_page_t)(intptr_t)lv_event_get_user_data(e));
}

// ── Menu list ─────────────────────────────────────────────────
static lv_obj_t* make_menu_list(lv_obj_t *parent) {
    lv_obj_t *v = lv_obj_create(parent);
    lv_obj_set_size(v, UI_W, UI_H - UI_BAR_H - UI_BAR_H);
    lv_obj_set_pos(v, 0, UI_BAR_H);
    lv_obj_set_style_bg_color(v, UI_COL_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(v, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(v, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(v, 0, LV_PART_MAIN);
    lv_obj_clear_flag(v, LV_OBJ_FLAG_SCROLLABLE);

    // Edit Presets hint lists the current preset names.
    static char s_preset_hint[48];
    lv_snprintf(s_preset_hint, sizeof(s_preset_hint), "%s / %s / %s",
                g_state.preset[0].name, g_state.preset[1].name, g_state.preset[2].name);

    struct MenuItem { const char *label; const char *hint; sub_page_t sub; };
    MenuItem items[] = {
        {"Port Configuration", "Enable ports / edit names",          SUB_PORTS   },
        {"Edit Presets",       s_preset_hint,                        SUB_PRESETS },
        {"Output",             "PWM frequency, dimming curve",        SUB_DISPLAY },
        {"Diagnostics",        "Live readings, panel check & faults",  SUB_DIAG  },
#if ENABLE_FUSE_CYCLE_TEST
        {"Fuse Cycle Test",    "Temporary: Panel A fuse turn-on surge test", SUB_FUSETEST },
#endif
        {"About",              "Firmware, hardware & credits",       SUB_ABOUT   },
    };
    int n     = sizeof(items) / sizeof(items[0]);
    int row_h = (UI_H - UI_BAR_H - UI_BAR_H) / n;

    for (int i = 0; i < n; i++) {
        lv_obj_t *row = lv_obj_create(v);
        lv_obj_set_size(row, UI_W, row_h - 1);
        lv_obj_set_pos(row, 0, i * row_h);
        lv_obj_set_style_bg_color(row,
            i % 2 == 0 ? UI_COL_PANEL_BG : UI_COL_BG, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(row, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(row, UI_COL_ACCENT_DIM,
                                  LV_STATE_PRESSED | LV_PART_MAIN);

        lv_obj_t *chev = lv_label_create(row);
        lv_label_set_text(chev, ">");
        lv_obj_set_style_text_color(chev, UI_COL_TEXT_DIM, LV_PART_MAIN);
        lv_obj_set_style_text_font(chev, UI_FONT_VALUE, LV_PART_MAIN);
        lv_obj_align(chev, LV_ALIGN_RIGHT_MID, -UI_SAFE, 0);

        lv_obj_t *lbl = lv_label_create(row);
        lv_label_set_text(lbl, items[i].label);
        lv_obj_set_style_text_color(lbl, UI_COL_TEXT, LV_PART_MAIN);
        lv_obj_set_style_text_font(lbl, UI_FONT_LABEL, LV_PART_MAIN);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, UI_SAFE, -6);

        lv_obj_t *hint = lv_label_create(row);
        lv_label_set_text(hint, items[i].hint);
        lv_obj_set_style_text_color(hint, UI_COL_TEXT_DIM, LV_PART_MAIN);
        lv_obj_set_style_text_font(hint, UI_FONT_SMALL, LV_PART_MAIN);
        lv_obj_align(hint, LV_ALIGN_LEFT_MID, UI_SAFE, 8);

        lv_obj_t *div = lv_obj_create(v);
        lv_obj_set_size(div, UI_W, 1);
        lv_obj_set_pos(div, 0, (i + 1) * row_h - 1);
        lv_obj_set_style_bg_color(div, UI_COL_PANEL_BDR, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(div, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(div, 0, LV_PART_MAIN);

        lv_obj_add_event_cb(row, row_cb, LV_EVENT_CLICKED,
                            (void*)(intptr_t)items[i].sub);
    }
    return v;
}

// ── Public ────────────────────────────────────────────────────
static void main_cb(lv_event_t *e) { screen_manager_goto(SCR_MAIN); }

lv_obj_t* screen_menu_create_diag(int return_panel) {
    lv_obj_t *scr = screen_menu_create();
    s_diag_return_panel = return_panel;
    show_sub(SUB_DIAG);
    return scr;
}

lv_obj_t* screen_menu_create() {
    s_diag_return_panel = -1;
    s_sub_view = nullptr;
    s_ne       = {};   // clear any stale port name-edit state
    s_pne      = {};   // clear any stale preset name-edit state

    s_scr = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(s_scr, UI_COL_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

    // Top bar
    lv_obj_t *topbar = lv_obj_create(s_scr);
    lv_obj_set_size(topbar, UI_W, UI_BAR_H);
    lv_obj_set_pos(topbar, 0, 0);
    ui_style_bar(topbar);
    lv_obj_set_style_border_side(topbar, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);

    lv_obj_t *lbl_t = lv_label_create(topbar);
    lv_label_set_text(lbl_t, "Settings");
    lv_obj_set_style_text_color(lbl_t, UI_COL_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl_t, UI_FONT_LABEL, LV_PART_MAIN);
    lv_obj_align(lbl_t, LV_ALIGN_LEFT_MID, UI_SAFE, 0);

    // Bottom bar
    lv_obj_t *botbar = lv_obj_create(s_scr);
    lv_obj_set_size(botbar, UI_W, UI_BAR_H);
    lv_obj_set_pos(botbar, 0, UI_H - UI_BAR_H);
    ui_style_bar(botbar);
    lv_obj_set_style_border_side(botbar, LV_BORDER_SIDE_TOP, LV_PART_MAIN);

    lv_obj_t *btn_back = lv_btn_create(botbar);
    lv_obj_set_size(btn_back, 120, UI_BAR_BTN_H);
    ui_hit_pad(btn_back, UI_HIT_PAD);
    lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, UI_SAFE, 0);
    ui_style_outline_btn(btn_back);
    lv_obj_set_style_pad_all(btn_back, 0, LV_PART_MAIN);
    lv_obj_t *lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, "< Main");
    lv_obj_set_style_text_color(lbl_back, UI_COL_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl_back, UI_FONT_SMALL, LV_PART_MAIN);
    lv_obj_center(lbl_back);
    lv_obj_add_event_cb(btn_back, main_cb, LV_EVENT_CLICKED, nullptr);

    s_list_view = make_menu_list(s_scr);

    return s_scr;
}
