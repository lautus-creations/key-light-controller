// SPDX-FileCopyrightText: 2026 DrEvan
// SPDX-License-Identifier: GPL-3.0-or-later

/*
 * screen_main.cpp - Main screen: one card per group (1-3), side by side.
 *
 *   Top bar:    [Key Light]  [Groups]  System [toggle]
 *   Card:       name + status dot | Bri row | CCT row | [On] [Detail >]
 *               (single-group card adds a preset row above the footer)
 *   Bottom bar: [Menu]  [Lock]
 */

#include "screens/screen_main.h"
#include "screens/screen_manager.h"
#include "lock.h"
#include "ui_theme.h"
#include "app_state.h"
#include "pwm.h"
#include "save_manager.h"
#include "fault_ui.h"
#include <Arduino.h>

// ── Per-card context ──────────────────────────────────────────
struct CardCtx {
    int       group_idx;
    lv_obj_t *sl_bri;
    lv_obj_t *sl_cct;
    lv_obj_t *lbl_bri_val;
    lv_obj_t *lbl_cct_val;
    lv_obj_t *btn_on;
    lv_obj_t *dot;        // status dot in card header
};

static CardCtx   s_cards[APP_MAX_GROUPS];
static lv_obj_t *s_sys_bg    = nullptr;  // toggle pill background
static lv_obj_t *s_sys_thumb = nullptr;  // toggle thumb circle

// ── PWM apply ────────────────────────────────────────────────
static void apply_group(int g) {
    if (!g_state.system_on) return;
    for (int i = 0; i < g_state.groups[g].count; i++) {
        int p = g_state.groups[g].ports[i];
        pwm_set_panel(p,
            g_state.panel_on[p] ? g_state.brightness[p] : 0,
            g_state.cct[p]);
    }
}

static void apply_all() {
    if (!g_state.system_on) { pwm_all_off(); return; }
    for (int g = 0; g < g_state.group_count; g++) apply_group(g);
}

// ── Refresh helpers ───────────────────────────────────────────
static void refresh_card(int ci) {
    CardCtx &c = s_cards[ci];
    int g      = c.group_idx;
    char buf[12];

    lv_snprintf(buf, sizeof(buf), "%d%%", group_brightness(g));
    lv_label_set_text(c.lbl_bri_val, buf);

    lv_snprintf(buf, sizeof(buf), "%dK", group_cct(g));
    lv_label_set_text(c.lbl_cct_val, buf);

    lv_slider_set_value(c.sl_bri, (int32_t)group_brightness(g), LV_ANIM_OFF);
    lv_slider_set_value(c.sl_cct, (int32_t)group_cct(g),        LV_ANIM_OFF);

    bool on = group_on(g) && g_state.system_on;

    if (c.dot)
        lv_obj_set_style_bg_color(c.dot, fault_dot_color(g, on), LV_PART_MAIN);

    if (c.btn_on) {
        lv_obj_set_style_bg_color(c.btn_on,
            on ? lv_color_hex(0x061008) : lv_color_hex(0x100606), LV_PART_MAIN);
        lv_obj_set_style_border_color(c.btn_on,
            on ? lv_color_hex(0x1A4030) : lv_color_hex(0x3A1010), LV_PART_MAIN);
        lv_obj_t *ol = lv_obj_get_child(c.btn_on, 0);
        if (ol) {
            lv_label_set_text(ol, on ? "On" : "Off");
            lv_obj_set_style_text_color(ol,
                on ? UI_COL_GOOD : lv_color_hex(0x884040), LV_PART_MAIN);
        }
    }
}

static void refresh_sys_toggle() {
    if (!s_sys_bg || !s_sys_thumb) return;
    bool on = g_state.system_on;
    lv_obj_set_style_bg_color(s_sys_bg,
        on ? UI_COL_GOOD : lv_color_hex(0x3A1010), LV_PART_MAIN);
    lv_obj_set_style_border_color(s_sys_bg,
        on ? lv_color_hex(0x1A6040) : lv_color_hex(0x5A2020), LV_PART_MAIN);
    // 42px track, 18px thumb, 3px inset
    lv_obj_set_pos(s_sys_thumb, on ? 21 : 3, 3);
}

// ── Callbacks ─────────────────────────────────────────────────
static void bri_cb(lv_event_t *e) {
    CardCtx *c   = (CardCtx*)lv_event_get_user_data(e);
    uint8_t  val = (uint8_t)lv_slider_get_value(c->sl_bri);
    group_set_brightness(c->group_idx, val);
    char buf[8]; lv_snprintf(buf, sizeof(buf), "%d%%", val);
    lv_label_set_text(c->lbl_bri_val, buf);
    apply_group(c->group_idx);
    schedule_save();
}

static void cct_cb(lv_event_t *e) {
    CardCtx  *c  = (CardCtx*)lv_event_get_user_data(e);
    uint16_t  val = (uint16_t)lv_slider_get_value(c->sl_cct);
    group_set_cct(c->group_idx, val);
    char buf[10]; lv_snprintf(buf, sizeof(buf), "%dK", val);
    lv_label_set_text(c->lbl_cct_val, buf);
    apply_group(c->group_idx);
    schedule_save();
}

// user_data = (card_idx << 4) | preset_idx
static void preset_cb(lv_event_t *e) {
    intptr_t data = (intptr_t)lv_event_get_user_data(e);
    int ci = (int)(data >> 4) & 0xF;
    int pi = (int)(data & 0xF);
    int g  = s_cards[ci].group_idx;
    group_set_cct(g, g_state.preset[pi].cct);   // presets set CCT only, brightness unchanged
    refresh_card(ci);
    apply_group(g);
    schedule_save();
}

static void on_cb(lv_event_t *e) {
    CardCtx *c = (CardCtx*)lv_event_get_user_data(e);
    group_set_on(c->group_idx, !group_on(c->group_idx));
    refresh_card((int)(c - s_cards));
    apply_group(c->group_idx);
    schedule_save();
}

static void detail_cb(lv_event_t *e) {
    int ci = (int)(intptr_t)lv_event_get_user_data(e);
    int p  = g_state.groups[s_cards[ci].group_idx].ports[0];
    screen_manager_goto_panel(p);
}

static void groups_cb(lv_event_t *e) { screen_manager_goto(SCR_GROUPS); }
static void menu_cb  (lv_event_t *e) { screen_manager_goto(SCR_MENU);   }

static void sys_cb(lv_event_t *e) {
    g_state.system_on = !g_state.system_on;
    apply_all();
    refresh_sys_toggle();
    for (int ci = 0; ci < g_state.group_count; ci++) refresh_card(ci);
    schedule_save();
}

// ── Nudge callbacks (+/- buttons on slider rows) ──────────────
// Brightness: step to the next multiple of 5%
static void bri_minus_cb(lv_event_t *e) {
    CardCtx *c = (CardCtx*)lv_event_get_user_data(e);
    int v  = (int)lv_slider_get_value(c->sl_bri);
    int nv = (v > 0) ? ((v - 1) / 5) * 5 : 0;
    if (nv < 0) nv = 0;
    lv_slider_set_value(c->sl_bri, nv, LV_ANIM_OFF);
    group_set_brightness(c->group_idx, (uint8_t)nv);
    char buf[8]; lv_snprintf(buf, sizeof(buf), "%d%%", nv);
    lv_label_set_text(c->lbl_bri_val, buf);
    apply_group(c->group_idx);
    schedule_save();
}
static void bri_plus_cb(lv_event_t *e) {
    CardCtx *c = (CardCtx*)lv_event_get_user_data(e);
    int v  = (int)lv_slider_get_value(c->sl_bri);
    int nv = ((v / 5) + 1) * 5;
    if (nv > 100) nv = 100;
    lv_slider_set_value(c->sl_bri, nv, LV_ANIM_OFF);
    group_set_brightness(c->group_idx, (uint8_t)nv);
    char buf[8]; lv_snprintf(buf, sizeof(buf), "%d%%", nv);
    lv_label_set_text(c->lbl_bri_val, buf);
    apply_group(c->group_idx);
    schedule_save();
}

// CCT: step to the next multiple of 100 K, clamped to 2700-6500
static void cct_minus_cb(lv_event_t *e) {
    CardCtx *c = (CardCtx*)lv_event_get_user_data(e);
    int v  = (int)lv_slider_get_value(c->sl_cct);
    int nv = ((v - 1) / 100) * 100;
    if (nv < 2700) nv = 2700;
    lv_slider_set_value(c->sl_cct, nv, LV_ANIM_OFF);
    group_set_cct(c->group_idx, (uint16_t)nv);
    char buf[10]; lv_snprintf(buf, sizeof(buf), "%dK", nv);
    lv_label_set_text(c->lbl_cct_val, buf);
    apply_group(c->group_idx);
    schedule_save();
}
static void cct_plus_cb(lv_event_t *e) {
    CardCtx *c = (CardCtx*)lv_event_get_user_data(e);
    int v  = (int)lv_slider_get_value(c->sl_cct);
    int nv = ((v / 100) + 1) * 100;
    if (nv > 6500) nv = 6500;
    lv_slider_set_value(c->sl_cct, nv, LV_ANIM_OFF);
    group_set_cct(c->group_idx, (uint16_t)nv);
    char buf[10]; lv_snprintf(buf, sizeof(buf), "%dK", nv);
    lv_label_set_text(c->lbl_cct_val, buf);
    apply_group(c->group_idx);
    schedule_save();
}

// ── Pill button helper ─────────────────────────────────────────
// Pill-shaped button with a centered label. Returns the button.
static lv_obj_t* make_pill_btn(lv_obj_t *parent,
                                int x, int y, int w, int h,
                                const char *text,
                                lv_color_t bg, lv_color_t bdr, lv_color_t txt,
                                const lv_font_t *font,
                                lv_event_cb_t cb, void *udata)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_style_radius(btn, h / 2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, bg, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn, bdr, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn, 0, LV_PART_MAIN);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, txt, LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl, font, LV_PART_MAIN);
    lv_obj_center(lbl);
    if (cb) lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, udata);
    return btn;
}

// ── Slider row helper ──────────────────────────────────────────
// One row: label | slider | [-] value [+].
// btn_sz > 0 adds nudge buttons around the value label.
// Returns the slider; the value label is written to *lbl_val_out.
static lv_obj_t* make_slider_row(lv_obj_t *parent,
                                  int iy, int iw,
                                  int lbl_w, int val_w, int track_h,
                                  const char *label_str, const char *val_str,
                                  lv_color_t lbl_col, lv_color_t val_col,
                                  const lv_font_t *font,
                                  int range_min, int range_max, int value,
                                  lv_event_cb_t cb, void *udata,
                                  lv_obj_t **lbl_val_out,
                                  bool is_cct = false,
                                  int btn_sz = 0,
                                  lv_event_cb_t minus_cb = nullptr,
                                  lv_event_cb_t plus_cb  = nullptr,
                                  void *btn_udata        = nullptr)
{
    const int BGAP = 2;   // nudge button to value label
    // The knob overhangs the track by 5px each side (pad=5 in ui_style_slider),
    // so both gaps must exceed 5 for the knob to clear its neighbors.
    const int LGAP = 10;
    const int RGAP = (btn_sz > 0) ? 8 : 10;

    // Right cluster: [-] gap value gap [+]
    int cluster_w = val_w + (btn_sz > 0 ? (2 * btn_sz + 2 * BGAP) : 0);
    int sl_w  = iw - lbl_w - LGAP - RGAP - cluster_w;
    int sl_x  = lbl_w + LGAP;
    int mx    = sl_x + sl_w + RGAP;              // minus button x (or value x)
    int vx    = mx   + (btn_sz > 0 ? btn_sz + BGAP : 0);  // value label x
    int px    = vx   + val_w + BGAP;             // plus button x

    // Center text and buttons on the track
    int fh      = (int)font->line_height;
    int track_cy = iy + track_h / 2;
    int text_y   = track_cy - fh / 2;
    int btn_y    = track_cy - btn_sz / 2;

    // Label (left)
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, label_str);
    lv_obj_set_style_text_font(lbl, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, lbl_col, LV_PART_MAIN);
    lv_obj_set_pos(lbl, 0, text_y);

    // Slider
    lv_obj_t *sl = lv_slider_create(parent);
    lv_obj_set_size(sl, sl_w, track_h);
    lv_obj_set_pos(sl, sl_x, iy);
    lv_slider_set_range(sl, range_min, range_max);
    lv_slider_set_value(sl, value, LV_ANIM_OFF);
    if (is_cct) ui_style_cct_slider(sl); else ui_style_slider(sl);
    if (cb) lv_obj_add_event_cb(sl, cb, LV_EVENT_VALUE_CHANGED, udata);

    // Value label
    lv_obj_t *vlbl = lv_label_create(parent);
    lv_label_set_text(vlbl, val_str);
    lv_obj_set_style_text_font(vlbl, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(vlbl, val_col, LV_PART_MAIN);
    lv_obj_set_pos(vlbl, vx, text_y);
    if (lbl_val_out) *lbl_val_out = vlbl;

    // Nudge buttons
    if (btn_sz > 0 && minus_cb && plus_cb) {
        auto make_nudge_btn = [&](int bx, const char *sym, lv_event_cb_t bcb) {
            lv_obj_t *b = lv_btn_create(parent);
            lv_obj_set_size(b, btn_sz, btn_sz);
            lv_obj_set_pos(b, bx, btn_y);
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
            lv_obj_set_style_text_font (bl, UI_FONT_BODY,            LV_PART_MAIN);
            lv_obj_center(bl);
            lv_obj_add_event_cb(b, bcb, LV_EVENT_CLICKED, btn_udata);
            ui_hit_pad(b, 3);
        };
        make_nudge_btn(mx, "-", minus_cb);
        make_nudge_btn(px, "+", plus_cb);
    }

    return sl;
}

// ── Card builder ──────────────────────────────────────────────
static void build_card(lv_obj_t *scr, int ci, int g, int nc,
                        int cx, int cy, int cw, int ch)
{
    s_cards[ci].group_idx = g;
    s_cards[ci].dot       = nullptr;

    // ── Card shell ────────────────────────────────────────────
    lv_obj_t *card = lv_obj_create(scr);
    lv_obj_set_size(card, cw, ch);
    lv_obj_set_pos(card, cx, cy);
    lv_obj_set_style_bg_color   (card, lv_color_hex(0x0B0B18), LV_PART_MAIN);
    lv_obj_set_style_bg_opa     (card, LV_OPA_COVER,           LV_PART_MAIN);
    lv_obj_set_style_border_color(card, lv_color_hex(0x1A1A2C), LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 1,                     LV_PART_MAIN);
    lv_obj_set_style_radius     (card, 8,                      LV_PART_MAIN);
    const int PAD = (nc == 1) ? 10 : 9;
    lv_obj_set_style_pad_all    (card, PAD,                    LV_PART_MAIN);
    lv_obj_clear_flag           (card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag             (card, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    int iw      = cw - 2 * PAD;
    int inner_h = ch - 2 * PAD;
    int iy      = 0;
    char buf[16];

    bool is_on = group_on(g) && g_state.system_on;

    // Group name: port names joined by " + "
    char nbuf[APP_NAME_LEN * APP_MAX_PORTS + 8];
    group_name_str(g, nbuf, sizeof(nbuf));

    // ── Slider column dimensions ──────────────────────────────
    const int SL_TH = 10;   // track height; knob is a 20px circle
    int lbl_w, val_w, btn_sz;
    if      (nc == 1) { lbl_w = 26; val_w = 44; btn_sz = 28; }
    else if (nc == 2) { lbl_w = 20; val_w = 40; btn_sz = 24; }
    else              { lbl_w = 20; val_w = 38; btn_sz =  0; }  // no room for nudge buttons
    lv_color_t sl_lbl_col = lv_color_hex(nc == 1 ? 0x5878A0 : 0x4A6080);
    lv_color_t sl_val_col = lv_color_hex(0x6090C0);
    const lv_font_t *sl_font = (nc == 1) ? UI_FONT_BODY : UI_FONT_SMALL;
    const lv_font_t *nm_font = (nc == 1) ? UI_FONT_LABEL : UI_FONT_SMALL;

    // ── Row / gap sizing ──────────────────────────────────────
    const int HDR_H = (nc == 1) ? 22 : 18;
    // track (10) + knob overhang (2x5) + 2px margin
    const int ROW_H = 22;
    const int FTR_H = (nc == 1) ? 28 : 24;
    const int G1    = (nc == 1) ? 12 : 12;   // after header
    const int G2    = (nc == 1) ? 14 : 10;   // after bri row
    const int G3    = (nc == 1) ? 12 : 10;   // after cct row

    int total;
    if (nc == 1) {
        const int PB_H = 36;
        const int G4   = 12;
        total = HDR_H + G1 + ROW_H + G2 + ROW_H + G3 + PB_H + G4 + FTR_H;
        iy = (inner_h - total) / 2;
        if (iy < 0) iy = 0;

        // Header: name + dot
        lv_obj_t *ln = lv_label_create(card);
        lv_label_set_text(ln, nbuf);
        lv_label_set_long_mode(ln, LV_LABEL_LONG_DOT);
        lv_obj_set_width(ln, iw - 16);
        lv_obj_set_style_text_color(ln, lv_color_hex(0x7898C8), LV_PART_MAIN);
        lv_obj_set_style_text_font(ln, nm_font, LV_PART_MAIN);
        lv_obj_set_pos(ln, 0, iy + 3);

        lv_obj_t *dot = lv_obj_create(card);
        lv_obj_set_size(dot, 8, 8);
        lv_obj_set_pos(dot, iw - 8, iy + 7);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_color(dot, fault_dot_color(g, is_on), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(dot, 0, LV_PART_MAIN);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
        s_cards[ci].dot = dot;
        iy += HDR_H + G1;

        // Bri slider row
        lv_snprintf(buf, sizeof(buf), "%d%%", group_brightness(g));
        s_cards[ci].sl_bri = make_slider_row(card, iy, iw, lbl_w, val_w, SL_TH,
            "Bri", buf, sl_lbl_col, sl_val_col, sl_font,
            0, 100, group_brightness(g),
            bri_cb, &s_cards[ci], &s_cards[ci].lbl_bri_val,
            false, btn_sz, bri_minus_cb, bri_plus_cb, &s_cards[ci]);
        iy += ROW_H + G2;

        // CCT slider row
        lv_snprintf(buf, sizeof(buf), "%dK", group_cct(g));
        s_cards[ci].sl_cct = make_slider_row(card, iy, iw, lbl_w, val_w, SL_TH,
            "CCT", buf, sl_lbl_col, sl_val_col, sl_font,
            2700, 6500, group_cct(g),
            cct_cb, &s_cards[ci], &s_cards[ci].lbl_cct_val,
            true, btn_sz, cct_minus_cb, cct_plus_cb, &s_cards[ci]);
        iy += ROW_H + G3;

        // Preset row: name + CCT hint per button
        int pbw = (iw - 2 * 5) / APP_PRESET_CNT;
        for (int pi = 0; pi < APP_PRESET_CNT; pi++) {
            lv_obj_t *pb = lv_btn_create(card);
            lv_obj_set_size(pb, pbw, PB_H);
            lv_obj_set_pos(pb, pi * (pbw + 5), iy);
            lv_obj_set_style_bg_color(pb, lv_color_hex(0x0A0A16), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(pb, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_border_color(pb, lv_color_hex(0x181828), LV_PART_MAIN);
            lv_obj_set_style_border_width(pb, 1, LV_PART_MAIN);
            lv_obj_set_style_radius(pb, 6, LV_PART_MAIN);
            lv_obj_set_style_pad_all(pb, 2, LV_PART_MAIN);
            lv_obj_set_style_bg_color(pb, lv_color_hex(0x070E1C),
                                      LV_STATE_PRESSED | LV_PART_MAIN);
            lv_obj_t *pn = lv_label_create(pb);
            lv_label_set_text(pn, g_state.preset[pi].name);
            lv_obj_set_style_text_font(pn, UI_FONT_SMALL, LV_PART_MAIN);
            lv_obj_set_style_text_color(pn, lv_color_hex(0x587098), LV_PART_MAIN);
            lv_obj_align(pn, LV_ALIGN_CENTER, 0, -7);
            char hint[16];
            lv_snprintf(hint, sizeof(hint), "%dK", g_state.preset[pi].cct);
            lv_obj_t *ph = lv_label_create(pb);
            lv_label_set_text(ph, hint);
            lv_obj_set_style_text_font(ph, UI_FONT_SMALL, LV_PART_MAIN);
            lv_obj_set_style_text_color(ph, lv_color_hex(0x303848), LV_PART_MAIN);
            lv_obj_align(ph, LV_ALIGN_CENTER, 0, 7);
            lv_obj_add_event_cb(pb, preset_cb, LV_EVENT_CLICKED,
                                (void*)(intptr_t)((ci << 4) | pi));
        }
        iy += PB_H + G4;

        // Footer: [On/Off]  [Detail >]
        int abw = (iw - 6) / 2;
        lv_obj_t *bon = make_pill_btn(card, 0, iy, abw, FTR_H,
            is_on ? "On" : "Off",
            is_on ? lv_color_hex(0x061008) : lv_color_hex(0x100606),
            is_on ? lv_color_hex(0x1A4030) : lv_color_hex(0x3A1010),
            is_on ? UI_COL_GOOD : lv_color_hex(0x884040),
            UI_FONT_BODY, on_cb, &s_cards[ci]);
        s_cards[ci].btn_on = bon;

        make_pill_btn(card, abw + 6, iy, abw, FTR_H,
            "Detail >",
            lv_color_hex(0x0C1020), lv_color_hex(0x202838),
            lv_color_hex(0x607090),
            UI_FONT_BODY, detail_cb, (void*)(intptr_t)ci);

    } else {
        // ── Compact multi-card layout ──
        total = HDR_H + G1 + ROW_H + G2 + ROW_H + G3 + FTR_H;
        // 18+12+22+10+22+10+24 = 118px, centered vertically
        iy = (inner_h - total) / 2;
        if (iy < 0) iy = 0;

        // Header: name + dot
        lv_obj_t *ln = lv_label_create(card);
        lv_label_set_text(ln, nbuf);
        lv_label_set_long_mode(ln, LV_LABEL_LONG_DOT);
        lv_obj_set_width(ln, iw - 12);
        lv_obj_set_style_text_color(ln, lv_color_hex(0x8098C0), LV_PART_MAIN);
        lv_obj_set_style_text_font(ln, nm_font, LV_PART_MAIN);
        lv_obj_set_pos(ln, 0, iy + 3);

        lv_obj_t *dot = lv_obj_create(card);
        lv_obj_set_size(dot, 6, 6);
        lv_obj_set_pos(dot, iw - 6, iy + 6);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_color(dot, fault_dot_color(g, is_on), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(dot, 0, LV_PART_MAIN);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
        s_cards[ci].dot = dot;
        iy += HDR_H + G1;

        // Bri slider row
        lv_snprintf(buf, sizeof(buf), "%d%%", group_brightness(g));
        s_cards[ci].sl_bri = make_slider_row(card, iy, iw, lbl_w, val_w, SL_TH,
            "Bri", buf, sl_lbl_col, sl_val_col, sl_font,
            0, 100, group_brightness(g),
            bri_cb, &s_cards[ci], &s_cards[ci].lbl_bri_val,
            false, btn_sz, bri_minus_cb, bri_plus_cb, &s_cards[ci]);
        iy += ROW_H + G2;

        // CCT slider row
        lv_snprintf(buf, sizeof(buf), "%dK", group_cct(g));
        s_cards[ci].sl_cct = make_slider_row(card, iy, iw, lbl_w, val_w, SL_TH,
            "CCT", buf, sl_lbl_col, sl_val_col, sl_font,
            2700, 6500, group_cct(g),
            cct_cb, &s_cards[ci], &s_cards[ci].lbl_cct_val,
            true, btn_sz, cct_minus_cb, cct_plus_cb, &s_cards[ci]);
        iy += ROW_H + G3;

        // Footer: [On/Off]  [Detail >]
        int abw = (iw - 6) / 2;
        lv_obj_t *bon = make_pill_btn(card, 0, iy, abw, FTR_H,
            is_on ? "On" : "Off",
            is_on ? lv_color_hex(0x061008) : lv_color_hex(0x100606),
            is_on ? lv_color_hex(0x1A4030) : lv_color_hex(0x3A1010),
            is_on ? UI_COL_GOOD : lv_color_hex(0x884040),
            UI_FONT_SMALL, on_cb, &s_cards[ci]);
        s_cards[ci].btn_on = bon;

        make_pill_btn(card, abw + 6, iy, abw, FTR_H,
            "Detail >",
            lv_color_hex(0x0C1020), lv_color_hex(0x202838),
            lv_color_hex(0x607090),
            UI_FONT_SMALL, detail_cb, (void*)(intptr_t)ci);
    }
}

// ── Public ────────────────────────────────────────────────────
lv_obj_t* screen_main_create() {
    s_sys_bg    = nullptr;
    s_sys_thumb = nullptr;

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

    lv_obj_t *lbl_title = lv_label_create(topbar);
    lv_label_set_text(lbl_title, "Key Light");
    lv_obj_set_style_text_color(lbl_title, lv_color_hex(0x90A8C8), LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl_title, UI_FONT_LABEL, LV_PART_MAIN);
    lv_obj_align(lbl_title, LV_ALIGN_LEFT_MID, UI_SAFE, 0);

    // Right cluster, built right to left: toggle, "System" label, Groups pill
    s_sys_bg = lv_obj_create(topbar);
    lv_obj_set_size(s_sys_bg, 42, 24);
    lv_obj_align(s_sys_bg, LV_ALIGN_RIGHT_MID, -UI_SAFE, 0);
    lv_obj_set_style_radius(s_sys_bg, 12, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_sys_bg, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_sys_bg, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_sys_bg, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_sys_bg, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_add_event_cb(s_sys_bg, sys_cb, LV_EVENT_CLICKED, nullptr);
    ui_hit_pad(s_sys_bg, 10);   // small toggle, larger touch target

    s_sys_thumb = lv_obj_create(s_sys_bg);
    lv_obj_set_size(s_sys_thumb, 18, 18);
    lv_obj_set_style_radius(s_sys_thumb, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_sys_thumb, lv_color_hex(0xD8E8F8), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_sys_thumb, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_sys_thumb, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_sys_thumb, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    refresh_sys_toggle();   // sets colors and thumb position

    lv_obj_t *lbl_sys_lbl = lv_label_create(topbar);
    lv_label_set_text(lbl_sys_lbl, "System");
    lv_obj_set_style_text_color(lbl_sys_lbl, lv_color_hex(0x304050), LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl_sys_lbl, UI_FONT_SMALL, LV_PART_MAIN);
    // 6px left of the 42px toggle
    lv_obj_align(lbl_sys_lbl, LV_ALIGN_RIGHT_MID, -UI_SAFE - 42 - 6, 0);

    lv_obj_t *btn_grp = lv_btn_create(topbar);
    lv_obj_set_size(btn_grp, 90, UI_BAR_BTN_H);
    ui_hit_pad(btn_grp, UI_HIT_PAD);
    // Left of the "System" label (~52px wide) with a 14px gap
    lv_obj_align(btn_grp, LV_ALIGN_RIGHT_MID, -UI_SAFE - 42 - 6 - 52 - 14, 0);
    lv_obj_set_style_radius(btn_grp, 14, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn_grp, lv_color_hex(0x080F1E), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn_grp, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn_grp, lv_color_hex(0x2A4878), LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_grp, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn_grp, 0, LV_PART_MAIN);
    lv_obj_t *lbl_grp = lv_label_create(btn_grp);
    lv_label_set_text(lbl_grp, "Groups");
    lv_obj_set_style_text_color(lbl_grp, lv_color_hex(0x78B0F0), LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl_grp, UI_FONT_SMALL, LV_PART_MAIN);
    lv_obj_center(lbl_grp);
    lv_obj_add_event_cb(btn_grp, groups_cb, LV_EVENT_CLICKED, nullptr);

    // ── Rule under the top bar ─────────────────────────────────
    lv_obj_t *rule = lv_obj_create(scr);
    lv_obj_set_size(rule, UI_W, 1);
    lv_obj_set_pos(rule, 0, UI_BAR_H);
    lv_obj_set_style_bg_color(rule, lv_color_hex(0x111120), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(rule, 0, LV_PART_MAIN);
    lv_obj_clear_flag(rule, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    // ── Bottom bar ────────────────────────────────────────────
    lv_obj_t *botbar = lv_obj_create(scr);
    lv_obj_set_size(botbar, UI_W, UI_BAR_H);
    lv_obj_set_pos(botbar, 0, UI_H - UI_BAR_H);
    ui_style_bar(botbar);
    lv_obj_set_style_border_side(botbar, LV_BORDER_SIDE_TOP, LV_PART_MAIN);

    lv_obj_t *btn_menu = lv_btn_create(botbar);
    lv_obj_set_size(btn_menu, 120, UI_BAR_BTN_H);
    ui_hit_pad(btn_menu, UI_HIT_PAD);
    lv_obj_align(btn_menu, LV_ALIGN_LEFT_MID, UI_SAFE, 0);
    lv_obj_set_style_bg_opa(btn_menu, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn_menu, lv_color_hex(0x1E2030), LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_menu, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(btn_menu, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn_menu, 0, LV_PART_MAIN);
    lv_obj_t *lbl_menu = lv_label_create(btn_menu);
    lv_label_set_text(lbl_menu, "Menu");
    lv_obj_set_style_text_color(lbl_menu, lv_color_hex(0x5A6880), LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl_menu, UI_FONT_SMALL, LV_PART_MAIN);
    lv_obj_center(lbl_menu);
    lv_obj_add_event_cb(btn_menu, menu_cb, LV_EVENT_CLICKED, nullptr);

    // Lock: screen lock, hold anywhere to unlock
    lv_obj_t *btn_lock = lv_btn_create(botbar);
    lv_obj_set_size(btn_lock, 120, UI_BAR_BTN_H);
    ui_hit_pad(btn_lock, UI_HIT_PAD);
    lv_obj_align(btn_lock, LV_ALIGN_RIGHT_MID, -UI_SAFE, 0);
    lv_obj_set_style_bg_opa(btn_lock, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn_lock, lv_color_hex(0x1E2030), LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_lock, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(btn_lock, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn_lock, 0, LV_PART_MAIN);
    lv_obj_t *lbl_lock = lv_label_create(btn_lock);
    lv_label_set_text(lbl_lock, "Lock");
    lv_obj_set_style_text_color(lbl_lock, lv_color_hex(0x5A6880), LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl_lock, UI_FONT_SMALL, LV_PART_MAIN);
    lv_obj_center(lbl_lock);
    lv_obj_add_event_cb(btn_lock, [](lv_event_t *e) { lock_engage(); }, LV_EVENT_CLICKED, nullptr);

    // ── Group cards ───────────────────────────────────────────
    int nc = (g_state.group_count > 0) ? g_state.group_count : 1;
    if (nc > APP_MAX_GROUPS) nc = APP_MAX_GROUPS;

    const int CY  = UI_BAR_H + 8;          // 8px margin below top bar
    const int CH  = UI_H - 2*UI_BAR_H - 16; // 8px margin above bottom bar
    const int CX0 = UI_SAFE;
    const int CW0 = UI_W - 2 * UI_SAFE;
    const int GAP = 6;
    const int CW  = (CW0 - GAP * (nc - 1)) / nc;

    for (int ci = 0; ci < nc; ci++)
        build_card(scr, ci, ci, nc, CX0 + ci * (CW + GAP), CY, CW, CH);

    return scr;
}

void screen_main_refresh() {
    for (int ci = 0; ci < g_state.group_count; ci++) refresh_card(ci);
    refresh_sys_toggle();
}
