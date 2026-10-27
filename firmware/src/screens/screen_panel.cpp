// SPDX-FileCopyrightText: 2026 DrEvan
// SPDX-License-Identifier: GPL-3.0-or-later

/*
 * screen_panel.cpp - Panel detail screen. Controls act on the whole group
 * containing the selected port.
 *
 *   Top bar:    [< Back]  name  [Groups] [SYS]
 *   Content:    brightness value/slider, CCT value/slider,
 *               preset x3 + On/Off row, fault strip (only when needed)
 *   Bottom bar: [< Prev]  [Next >]  (only with more than one group)
 */

#include "screens/screen_panel.h"
#include "screens/screen_manager.h"
#include "ui_theme.h"
#include "app_state.h"
#include "pwm.h"
#include "save_manager.h"
#include "fault.h"
#include "fault_ui.h"
#include <Arduino.h>
#include <cstdio>

static int s_port  = 0;   // primary port: read-back and display
static int s_group = 0;   // group containing s_port: all writes

static lv_obj_t *s_sl_bri      = nullptr;
static lv_obj_t *s_sl_cct      = nullptr;
static lv_obj_t *s_lbl_bri_val = nullptr;
static lv_obj_t *s_lbl_cct_val = nullptr;
static lv_obj_t *s_btn_on      = nullptr;
static lv_obj_t *s_btn_sys     = nullptr;

// Fault status strip
static lv_obj_t   *s_strip       = nullptr;
static lv_obj_t   *s_strip_lbl   = nullptr;
static lv_obj_t   *s_strip_reset = nullptr;
static lv_timer_t *s_strip_timer = nullptr;
static uint32_t    s_strip_gen   = 0;
static lv_obj_t   *s_strip_scr   = nullptr;   // screen that owns the strip + timer
static int         s_strip_w     = 0;         // cached; layout is not resolved at create time

// ── Apply ─────────────────────────────────────────────────────
// Drives PWM for every port in the group so linked panels move together.
static void apply() {
    for (int i = 0; i < g_state.groups[s_group].count; i++) {
        int p = g_state.groups[s_group].ports[i];
        if (!g_state.system_on) {
            pwm_set_panel((uint8_t)p, 0, g_state.cct[p]);
        } else {
            pwm_set_panel((uint8_t)p,
                g_state.panel_on[p] ? g_state.brightness[p] : 0,
                g_state.cct[p]);
        }
    }
}

// ── Refresh labels ────────────────────────────────────────────
static void refresh_labels() {
    char buf[12];
    lv_snprintf(buf, sizeof(buf), "%d%%", g_state.brightness[s_port]);
    lv_label_set_text(s_lbl_bri_val, buf);
    lv_snprintf(buf, sizeof(buf), "%dK",  g_state.cct[s_port]);
    lv_label_set_text(s_lbl_cct_val, buf);

    // Setting the value programmatically does not fire VALUE_CHANGED
    lv_slider_set_value(s_sl_bri, (int32_t)g_state.brightness[s_port], LV_ANIM_OFF);
    lv_slider_set_value(s_sl_cct, (int32_t)g_state.cct[s_port],        LV_ANIM_OFF);
}

static void refresh_on_btn() {
    bool on = g_state.panel_on[s_port] && g_state.system_on;
    lv_obj_set_style_bg_color(s_btn_on,
        on ? UI_COL_GOOD : UI_COL_OFF_LIT, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_btn_on, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_t *lbl = lv_obj_get_child(s_btn_on, 0);
    if (lbl) lv_label_set_text(lbl, on ? "On" : "Off");
}

static void refresh_sys_btn() {
    if (!s_btn_sys) return;
    lv_obj_set_style_bg_color(s_btn_sys,
        g_state.system_on ? UI_COL_GOOD : UI_COL_OFF_LIT, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_btn_sys, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_t *lbl = lv_obj_get_child(s_btn_sys, 0);
    if (lbl) lv_label_set_text(lbl, g_state.system_on ? "SYS ON" : "SYS OFF");
}

// ── Callbacks ─────────────────────────────────────────────────
static void bri_cb(lv_event_t *e) {
    uint8_t val = (uint8_t)lv_slider_get_value(s_sl_bri);
    group_set_brightness(s_group, val);
    char buf[8]; lv_snprintf(buf, sizeof(buf), "%d%%", val);
    lv_label_set_text(s_lbl_bri_val, buf);
    apply();
    schedule_save();
}

static void cct_cb(lv_event_t *e) {
    uint16_t val = (uint16_t)lv_slider_get_value(s_sl_cct);
    group_set_cct(s_group, val);
    char buf[10]; lv_snprintf(buf, sizeof(buf), "%dK", val);
    lv_label_set_text(s_lbl_cct_val, buf);
    apply();
    schedule_save();
}

// user_data = preset index
static void preset_cb(lv_event_t *e) {
    int pi = (int)(intptr_t)lv_event_get_user_data(e);
    group_set_cct(s_group, g_state.preset[pi].cct);   // presets set CCT only, brightness unchanged
    refresh_labels();
    apply();
    schedule_save();
}

static void on_cb(lv_event_t *e) {
    group_set_on(s_group, !g_state.panel_on[s_port]);  // toggle based on primary port
    refresh_on_btn();
    apply();
    schedule_save();
}

static void sys_cb(lv_event_t *e) {
    g_state.system_on = !g_state.system_on;
    outputs_apply_all();   // System affects every port, not only this group
    refresh_sys_btn();
    refresh_on_btn();
    schedule_save();
}

// ── Fault status strip ────────────────────────────────────────
// Shows the most severe condition across the ports in this screen's group.
static void strip_refresh(bool force) {
    if (!s_strip) return;
    uint32_t gen = fault_generation();
    if (!force && gen == s_strip_gen) return;
    s_strip_gen = gen;

    int  worst = 0, worst_port = -1, n_issue = 0;   // 0 ok, 1 warn, 2 trip
    for (int i = 0; i < g_state.groups[s_group].count; i++) {
        int p = g_state.groups[s_group].ports[i];
        int lvl = fault_tripped((uint8_t)p) ? 2
                : (fault_flags((uint8_t)p) & FW_WARN_MASK) ? 1 : 0;
        if (lvl) n_issue++;
        if (lvl > worst) { worst = lvl; worst_port = p; }
    }

    if (worst == 0) {
        lv_obj_add_flag(s_strip, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    // Kept generic; details are on the Diagnostics page.
    char txt[96];
    if (n_issue > 1)
        snprintf(txt, sizeof(txt), "%d panels %s  -  Tap for Diagnostics >",
                 n_issue, worst == 2 ? "have a problem" : "need attention");
    else
        snprintf(txt, sizeof(txt), "%s %s  -  Tap for Diagnostics >",
                 g_state.name[worst_port], worst == 2 ? "shut off" : "needs attention");
    lv_label_set_text(s_strip_lbl, txt);

    lv_color_t bg  = worst == 2 ? lv_color_hex(0x2A0A0A) : lv_color_hex(0x2A1C06);
    lv_color_t bdr = worst == 2 ? UI_COL_OFF_LIT         : UI_COL_WARN;
    lv_color_t fg  = worst == 2 ? lv_color_hex(0xFF8A8A) : lv_color_hex(0xFFC070);
    lv_obj_set_style_bg_color(s_strip, bg, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_strip, bdr, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_strip_lbl, fg, LV_PART_MAIN);

    if (worst == 2) lv_obj_clear_flag(s_strip_reset, LV_OBJ_FLAG_HIDDEN);
    else            lv_obj_add_flag(s_strip_reset, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_width(s_strip_lbl, s_strip_w - 20 - (worst == 2 ? 86 : 0));
    lv_obj_clear_flag(s_strip, LV_OBJ_FLAG_HIDDEN);
}

static void strip_timer_cb(lv_timer_t *t) { strip_refresh(false); }

static void strip_tap_cb(lv_event_t *e) { screen_manager_goto_diag(s_port); }

static void strip_reset_cb(lv_event_t *e) {
    fault_ui_reset_all();
    strip_refresh(true);
}

static void scr_deleted_cb(lv_event_t *e) {
    // Prev/Next builds the new screen before lv_scr_load_anim deletes the
    // old one, so only tear down if this screen still owns the strip.
    if (lv_event_get_target(e) != s_strip_scr) return;
    s_strip_scr = nullptr;
    if (s_strip_timer) { lv_timer_del(s_strip_timer); s_strip_timer = nullptr; }
    s_strip = s_strip_lbl = s_strip_reset = nullptr;
}

static void back_cb   (lv_event_t *e) { screen_manager_goto(SCR_MAIN);   }
static void groups_cb (lv_event_t *e) { screen_manager_goto(SCR_GROUPS); }

// Group index containing s_port, or 0 if not found
static int find_port_group() {
    for (int gg = 0; gg < g_state.group_count; gg++)
        for (int ss = 0; ss < g_state.groups[gg].count; ss++)
            if (g_state.groups[gg].ports[ss] == s_port) return gg;
    return 0;
}

static void panel_prev_cb(lv_event_t *) {
    int cur  = find_port_group();
    int prev = (cur - 1 + g_state.group_count) % g_state.group_count;
    screen_manager_goto_panel_prev(g_state.groups[prev].ports[0]);
}

static void panel_next_cb(lv_event_t *) {
    int cur  = find_port_group();
    int next = (cur + 1) % g_state.group_count;
    screen_manager_goto_panel(g_state.groups[next].ports[0]);
}

// ── Nudge callbacks: step to next multiple of 5% / 100 K ──────
static void panel_bri_minus_cb(lv_event_t *) {
    int v  = (int)lv_slider_get_value(s_sl_bri);
    int nv = (v > 0) ? ((v - 1) / 5) * 5 : 0;
    if (nv < 0) nv = 0;
    lv_slider_set_value(s_sl_bri, nv, LV_ANIM_OFF);
    group_set_brightness(s_group, (uint8_t)nv);
    char buf[8]; lv_snprintf(buf, sizeof(buf), "%d%%", nv);
    lv_label_set_text(s_lbl_bri_val, buf);
    apply(); schedule_save();
}
static void panel_bri_plus_cb(lv_event_t *) {
    int v  = (int)lv_slider_get_value(s_sl_bri);
    int nv = ((v / 5) + 1) * 5;
    if (nv > 100) nv = 100;
    lv_slider_set_value(s_sl_bri, nv, LV_ANIM_OFF);
    group_set_brightness(s_group, (uint8_t)nv);
    char buf[8]; lv_snprintf(buf, sizeof(buf), "%d%%", nv);
    lv_label_set_text(s_lbl_bri_val, buf);
    apply(); schedule_save();
}
static void panel_cct_minus_cb(lv_event_t *) {
    int v  = (int)lv_slider_get_value(s_sl_cct);
    int nv = ((v - 1) / 100) * 100;
    if (nv < 2700) nv = 2700;
    lv_slider_set_value(s_sl_cct, nv, LV_ANIM_OFF);
    group_set_cct(s_group, (uint16_t)nv);
    char buf[10]; lv_snprintf(buf, sizeof(buf), "%dK", nv);
    lv_label_set_text(s_lbl_cct_val, buf);
    apply(); schedule_save();
}
static void panel_cct_plus_cb(lv_event_t *) {
    int v  = (int)lv_slider_get_value(s_sl_cct);
    int nv = ((v / 100) + 1) * 100;
    if (nv > 6500) nv = 6500;
    lv_slider_set_value(s_sl_cct, nv, LV_ANIM_OFF);
    group_set_cct(s_group, (uint16_t)nv);
    char buf[10]; lv_snprintf(buf, sizeof(buf), "%dK", nv);
    lv_label_set_text(s_lbl_cct_val, buf);
    apply(); schedule_save();
}

// Nudge button, absolutely positioned on parent
static void make_panel_nudge_btn(lv_obj_t *parent,
                                  int x, int y, int sz,
                                  const char *sym, lv_event_cb_t cb) {
    lv_obj_t *b = lv_btn_create(parent);
    lv_obj_set_size(b, sz, sz);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_style_radius(b, 3, LV_PART_MAIN);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x080C1A), LV_PART_MAIN);
    lv_obj_set_style_bg_opa  (b, LV_OPA_COVER,           LV_PART_MAIN);
    lv_obj_set_style_border_color(b, lv_color_hex(0x1A2840), LV_PART_MAIN);
    lv_obj_set_style_border_width(b, 1,                       LV_PART_MAIN);
    lv_obj_set_style_pad_all (b, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x1A2848),
                              LV_STATE_PRESSED | LV_PART_MAIN);
    lv_obj_t *bl = lv_label_create(b);
    lv_label_set_text(bl, sym);
    lv_obj_set_style_text_color(bl, lv_color_hex(0x6888B8), LV_PART_MAIN);
    lv_obj_set_style_text_font (bl, UI_FONT_VALUE,           LV_PART_MAIN);
    lv_obj_center(bl);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
}

// ── Public ────────────────────────────────────────────────────
lv_obj_t* screen_panel_create(int port_idx) {
    s_port     = port_idx;
    s_group    = find_port_group();
    s_sl_bri   = nullptr;
    s_sl_cct   = nullptr;
    s_btn_on   = nullptr;
    s_btn_sys  = nullptr;

    lv_obj_t *scr = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(scr, UI_COL_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // ── Top bar ───────────────────────────────────────────────
    lv_obj_t *topbar = lv_obj_create(scr);
    lv_obj_set_size(topbar, UI_W, UI_BAR_H);
    lv_obj_set_pos(topbar, 0, 0);
    ui_style_bar(topbar);
    lv_obj_set_style_border_side(topbar, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);

    lv_obj_t *btn_back = lv_btn_create(topbar);
    lv_obj_set_size(btn_back, 80, UI_BAR_BTN_H);
    lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, UI_SAFE, 0);
    ui_hit_pad(btn_back, UI_HIT_PAD);
    ui_style_outline_btn(btn_back);
    lv_obj_set_style_pad_all(btn_back, 0, LV_PART_MAIN);
    lv_obj_t *lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, "< Back");
    lv_obj_set_style_text_color(lbl_back, UI_COL_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl_back, UI_FONT_SMALL, LV_PART_MAIN);
    lv_obj_center(lbl_back);
    lv_obj_add_event_cb(btn_back, back_cb, LV_EVENT_CLICKED, nullptr);

    // Panel name, or all names in the group joined by " + "
    {
        char pname[APP_NAME_LEN * APP_MAX_PORTS + 8];
        int port_grp = -1;
        for (int gg = 0; gg < g_state.group_count && port_grp < 0; gg++)
            for (int ss = 0; ss < g_state.groups[gg].count; ss++)
                if (g_state.groups[gg].ports[ss] == s_port) { port_grp = gg; break; }
        if (port_grp >= 0)
            group_name_str(port_grp, pname, sizeof(pname));
        else
            lv_snprintf(pname, sizeof(pname), "%s", g_state.name[s_port]);

        const int NAME_LX  = UI_SAFE + 80 + 8;             // Back (80) + gap
        const int NAME_RMG = UI_SAFE + 84 + 10 + 80 + 10;  // SYS (84) + Groups (80) + gaps

        lv_obj_t *lbl_name = lv_label_create(topbar);
        lv_label_set_text(lbl_name, pname);
        lv_label_set_long_mode(lbl_name, LV_LABEL_LONG_DOT);
        lv_obj_set_width(lbl_name, UI_W - NAME_LX - NAME_RMG);
        lv_obj_set_style_text_color(lbl_name, UI_COL_TEXT, LV_PART_MAIN);
        lv_obj_set_style_text_font(lbl_name, UI_FONT_SMALL, LV_PART_MAIN);
        lv_obj_align(lbl_name, LV_ALIGN_LEFT_MID, NAME_LX, 0);
    }

    // System on/off (right edge)
    s_btn_sys = lv_btn_create(topbar);
    lv_obj_set_size(s_btn_sys, 84, UI_BAR_BTN_H);
    lv_obj_align(s_btn_sys, LV_ALIGN_RIGHT_MID, -UI_SAFE, 0);
    ui_hit_pad(s_btn_sys, UI_HIT_PAD);
    lv_obj_set_style_radius(s_btn_sys, 4, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_btn_sys, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_btn_sys, 0, LV_PART_MAIN);
    lv_obj_t *lbl_sys = lv_label_create(s_btn_sys);
    lv_obj_set_style_text_color(lbl_sys, UI_COL_WHITE, LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl_sys, UI_FONT_SMALL, LV_PART_MAIN);
    lv_obj_center(lbl_sys);
    lv_obj_add_event_cb(s_btn_sys, sys_cb, LV_EVENT_CLICKED, nullptr);
    refresh_sys_btn();

    // Groups: 10px left of the 84px SYS button
    lv_obj_t *btn_grp = lv_btn_create(topbar);
    lv_obj_set_size(btn_grp, 80, UI_BAR_BTN_H);
    lv_obj_align(btn_grp, LV_ALIGN_RIGHT_MID, -(UI_SAFE + 84 + 10), 0);
    ui_hit_pad(btn_grp, UI_HIT_PAD);
    ui_style_preset_btn(btn_grp);
    lv_obj_set_style_pad_all(btn_grp, 0, LV_PART_MAIN);
    lv_obj_t *lbl_grp = lv_label_create(btn_grp);
    lv_label_set_text(lbl_grp, "Groups");
    lv_obj_set_style_text_color(lbl_grp, UI_COL_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl_grp, UI_FONT_SMALL, LV_PART_MAIN);
    lv_obj_center(lbl_grp);
    lv_obj_add_event_cb(btn_grp, groups_cb, LV_EVENT_CLICKED, nullptr);

    // ── Content ───────────────────────────────────────────────
    int cw = UI_W - 2 * UI_SAFE;   // 440
    int cx = UI_SAFE;
    int cy = UI_BAR_H + 6;

    // Nudge buttons sit at the right end of each value row; sliders run
    // full width below.
    const int BTN_SZ  = 36;
    const int BTN_GAP = 4;
    const int VAL_ROW_H = 38;   // UI_FONT_LARGE line height
    int btn_row_off = (VAL_ROW_H - BTN_SZ) / 2;

    char buf[12];

    // ── Brightness row ────────────────────────────────────────
    lv_snprintf(buf, sizeof(buf), "%d%%", g_state.brightness[s_port]);
    s_lbl_bri_val = lv_label_create(scr);
    lv_label_set_text(s_lbl_bri_val, buf);
    lv_obj_set_style_text_color(s_lbl_bri_val, UI_COL_WHITE, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_lbl_bri_val, UI_FONT_LARGE, LV_PART_MAIN);
    lv_obj_set_pos(s_lbl_bri_val, cx, cy);

    // Caption between the value and the nudge buttons
    lv_obj_t *lbl_bri_hdr = lv_label_create(scr);
    lv_label_set_text(lbl_bri_hdr, "Brightness");
    lv_obj_set_style_text_color(lbl_bri_hdr, UI_COL_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl_bri_hdr, UI_FONT_BODY, LV_PART_MAIN);
    lv_obj_set_pos(lbl_bri_hdr, cx + 100, cy + 12);

    {
        int bp_x = cx + cw - BTN_SZ;
        int bm_x = bp_x - BTN_GAP - BTN_SZ;
        int by   = cy + btn_row_off;
        make_panel_nudge_btn(scr, bm_x, by, BTN_SZ, "-", panel_bri_minus_cb);
        make_panel_nudge_btn(scr, bp_x, by, BTN_SZ, "+", panel_bri_plus_cb);
    }
    cy += VAL_ROW_H + 6;    // gap so buttons don't accidentally hit the slider (tightened for 46px bars)

    // Brightness slider
    s_sl_bri = lv_slider_create(scr);
    lv_obj_set_size(s_sl_bri, cw, 20);
    lv_obj_set_pos(s_sl_bri, cx, cy);
    lv_slider_set_range(s_sl_bri, 0, 100);
    lv_slider_set_value(s_sl_bri, (int32_t)g_state.brightness[s_port], LV_ANIM_OFF);
    ui_style_slider(s_sl_bri);
    lv_obj_add_event_cb(s_sl_bri, bri_cb, LV_EVENT_VALUE_CHANGED, nullptr);
    cy += 26;

    // ── Color Temp row ────────────────────────────────────────
    lv_snprintf(buf, sizeof(buf), "%dK", g_state.cct[s_port]);
    s_lbl_cct_val = lv_label_create(scr);
    lv_label_set_text(s_lbl_cct_val, buf);
    lv_obj_set_style_text_color(s_lbl_cct_val, UI_COL_WHITE, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_lbl_cct_val, UI_FONT_LARGE, LV_PART_MAIN);
    lv_obj_set_pos(s_lbl_cct_val, cx, cy);

    // Caption, offset right to clear "6500K"
    lv_obj_t *lbl_cct_hdr = lv_label_create(scr);
    lv_label_set_text(lbl_cct_hdr, "Color Temp");
    lv_obj_set_style_text_color(lbl_cct_hdr, UI_COL_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl_cct_hdr, UI_FONT_BODY, LV_PART_MAIN);
    lv_obj_set_pos(lbl_cct_hdr, cx + 120, cy + 12);

    {
        int bp_x = cx + cw - BTN_SZ;
        int bm_x = bp_x - BTN_GAP - BTN_SZ;
        int by   = cy + btn_row_off;
        make_panel_nudge_btn(scr, bm_x, by, BTN_SZ, "-", panel_cct_minus_cb);
        make_panel_nudge_btn(scr, bp_x, by, BTN_SZ, "+", panel_cct_plus_cb);
    }
    cy += VAL_ROW_H + 6;    // gap so buttons don't accidentally hit the slider (tightened for 46px bars)

    // Color Temp slider
    s_sl_cct = lv_slider_create(scr);
    lv_obj_set_size(s_sl_cct, cw, 20);
    lv_obj_set_pos(s_sl_cct, cx, cy);
    lv_slider_set_range(s_sl_cct, 2700, 6500);
    lv_slider_set_value(s_sl_cct, (int32_t)g_state.cct[s_port], LV_ANIM_OFF);
    ui_style_cct_slider(s_sl_cct);
    lv_obj_add_event_cb(s_sl_cct, cct_cb, LV_EVENT_VALUE_CHANGED, nullptr);
    cy += 26;

    // ── Preset + On/Off row ───────────────────────────────────
    // Four equal-width buttons. Preset names come from g_state (user-editable).
    int total_btns = APP_PRESET_CNT + 1;
    int btn_gap    = 5;
    int btn_w      = (cw - btn_gap * (total_btns - 1)) / total_btns;
    int btn_h      = 34;

    for (int pi = 0; pi < APP_PRESET_CNT; pi++) {
        lv_obj_t *pb = lv_btn_create(scr);
        lv_obj_set_size(pb, btn_w, btn_h);
        lv_obj_set_pos(pb, cx + pi * (btn_w + btn_gap), cy);
        ui_style_preset_btn(pb);
        lv_obj_set_style_pad_all(pb, 0, LV_PART_MAIN);
        lv_obj_t *pl = lv_label_create(pb);
        lv_label_set_text(pl, g_state.preset[pi].name);
        lv_obj_set_style_text_color(pl, UI_COL_TEXT, LV_PART_MAIN);
        lv_obj_set_style_text_font(pl, UI_FONT_SMALL, LV_PART_MAIN);
        lv_obj_center(pl);
        lv_obj_add_event_cb(pb, preset_cb, LV_EVENT_CLICKED, (void*)(intptr_t)pi);
    }

    s_btn_on = lv_btn_create(scr);
    lv_obj_set_size(s_btn_on, btn_w, btn_h);
    lv_obj_set_pos(s_btn_on, cx + APP_PRESET_CNT * (btn_w + btn_gap), cy);
    lv_obj_set_style_radius(s_btn_on, 4, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_btn_on, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_btn_on, 0, LV_PART_MAIN);
    lv_obj_t *lbl_on = lv_label_create(s_btn_on);
    lv_obj_set_style_text_color(lbl_on, UI_COL_WHITE, LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl_on, UI_FONT_SMALL, LV_PART_MAIN);
    lv_obj_center(lbl_on);
    lv_obj_add_event_cb(s_btn_on, on_cb, LV_EVENT_CLICKED, nullptr);
    refresh_on_btn();
    cy += btn_h;

    // ── Fault status strip (hidden unless the group has an issue) ──
    {
        int strip_y = cy + 5;
        int strip_h = (UI_H - UI_BAR_H) - strip_y - 4;     // fill the gap above the bottom bar
        if (strip_h > 34) strip_h = 34;
        s_strip = lv_obj_create(scr);
        s_strip_w = cw;
        lv_obj_set_size(s_strip, cw, strip_h);
        lv_obj_set_pos(s_strip, cx, strip_y);
        lv_obj_set_style_radius(s_strip, 6, LV_PART_MAIN);
        lv_obj_set_style_border_width(s_strip, 1, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_strip, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_pad_all(s_strip, 0, LV_PART_MAIN);
        lv_obj_clear_flag(s_strip, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(s_strip, LV_OBJ_FLAG_CLICKABLE);          // whole strip -> Diagnostics
        lv_obj_set_style_bg_opa(s_strip, LV_OPA_70, LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_add_event_cb(s_strip, strip_tap_cb, LV_EVENT_CLICKED, nullptr);

        s_strip_lbl = lv_label_create(s_strip);
        lv_label_set_long_mode(s_strip_lbl, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_font(s_strip_lbl, UI_FONT_BODY, LV_PART_MAIN);
        lv_obj_align(s_strip_lbl, LV_ALIGN_LEFT_MID, 10, 0);

        s_strip_reset = lv_btn_create(s_strip);
        lv_obj_set_size(s_strip_reset, 76, strip_h - 8);
        lv_obj_align(s_strip_reset, LV_ALIGN_RIGHT_MID, -4, 0);
        lv_obj_set_style_radius(s_strip_reset, 4, LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_strip_reset, UI_COL_OFF_LIT, LV_PART_MAIN);
        lv_obj_set_style_border_width(s_strip_reset, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(s_strip_reset, 0, LV_PART_MAIN);
        lv_obj_t *rl = lv_label_create(s_strip_reset);
        lv_label_set_text(rl, "Reset");
        lv_obj_set_style_text_font(rl, UI_FONT_BODY, LV_PART_MAIN);
        lv_obj_set_style_text_color(rl, UI_COL_WHITE, LV_PART_MAIN);
        lv_obj_center(rl);
        lv_obj_add_event_cb(s_strip_reset, strip_reset_cb, LV_EVENT_CLICKED, nullptr);

        strip_refresh(true);
        if (s_strip_timer) lv_timer_del(s_strip_timer);
        s_strip_timer = lv_timer_create(strip_timer_cb, 250, nullptr);
        s_strip_scr = scr;
        lv_obj_add_event_cb(scr, scr_deleted_cb, LV_EVENT_DELETE, nullptr);
    }

    // ── Bottom bar: Prev / Next group ─────────────────────────
    lv_obj_t *botbar = lv_obj_create(scr);
    lv_obj_set_size(botbar, UI_W, UI_BAR_H);
    lv_obj_set_pos(botbar, 0, UI_H - UI_BAR_H);
    ui_style_bar(botbar);
    lv_obj_set_style_border_side(botbar, LV_BORDER_SIDE_TOP, LV_PART_MAIN);

    if (g_state.group_count > 1) {
        lv_obj_t *btn_prev = lv_btn_create(botbar);
        lv_obj_set_size(btn_prev, 120, UI_BAR_BTN_H);
        lv_obj_align(btn_prev, LV_ALIGN_LEFT_MID, UI_SAFE, 0);
        ui_hit_pad(btn_prev, UI_HIT_PAD);
        ui_style_outline_btn(btn_prev);
        lv_obj_set_style_pad_all(btn_prev, 0, LV_PART_MAIN);
        lv_obj_t *lbl_prev = lv_label_create(btn_prev);
        lv_label_set_text(lbl_prev, "< Prev");
        lv_obj_set_style_text_color(lbl_prev, UI_COL_TEXT_DIM, LV_PART_MAIN);
        lv_obj_set_style_text_font(lbl_prev, UI_FONT_SMALL, LV_PART_MAIN);
        lv_obj_center(lbl_prev);
        lv_obj_add_event_cb(btn_prev, panel_prev_cb, LV_EVENT_CLICKED, nullptr);

        lv_obj_t *btn_next = lv_btn_create(botbar);
        lv_obj_set_size(btn_next, 120, UI_BAR_BTN_H);
        lv_obj_align(btn_next, LV_ALIGN_RIGHT_MID, -UI_SAFE, 0);
        ui_hit_pad(btn_next, UI_HIT_PAD);
        ui_style_outline_btn(btn_next);
        lv_obj_set_style_pad_all(btn_next, 0, LV_PART_MAIN);
        lv_obj_t *lbl_next = lv_label_create(btn_next);
        lv_label_set_text(lbl_next, "Next >");
        lv_obj_set_style_text_color(lbl_next, UI_COL_TEXT_DIM, LV_PART_MAIN);
        lv_obj_set_style_text_font(lbl_next, UI_FONT_SMALL, LV_PART_MAIN);
        lv_obj_center(lbl_next);
        lv_obj_add_event_cb(btn_next, panel_next_cb, LV_EVENT_CLICKED, nullptr);
    }

    return scr;
}
