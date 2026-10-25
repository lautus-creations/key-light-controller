// SPDX-FileCopyrightText: 2026 DrEvan
// SPDX-License-Identifier: GPL-3.0-or-later

/*
 * screen_diag.cpp - Menu > Diagnostics: live sense readout.
 *
 * Vertical layout is derived from UI_BAR_H.
 *
 *   [Diagnostics                                   < Back]   header bar
 *    Panel     Rail     Warm          Cool          24V      column heads
 *    Panel A   24.1 V   0.094 A 25%   0.094 A 25%   ON       value line   ┐
 *              OK                                            status line  ┘ x3
 *   ADCs 3/3 online             [Check] [System] [Reset]  footer
 *
 * Check  -> Panel Check view (panel_check.h): per-channel light test, pass/fail
 * System -> uptime, reset reason, last fault, I2C errors, peak currents
 */

#include "screens/screen_diag.h"
#include "ui_theme.h"
#include "app_state.h"
#include "sense.h"
#include "fault.h"
#include "fault_ui.h"
#include "pwm.h"
#include "panel_check.h"
#include "sysinfo.h"
#include "touch.h"
#include "sense_config.h"
#include "save_manager.h"
#include <Arduino.h>
#include <cstdio>
#include <cstring>

struct DiagRow {
    lv_obj_t *rail;
    lv_obj_t *warm;
    lv_obj_t *cool;
    lv_obj_t *hs;
    lv_obj_t *status;
};

static DiagRow     s_rows[3];
static lv_obj_t   *s_footer = nullptr;
static lv_obj_t   *s_reset  = nullptr;
static lv_timer_t *s_timer  = nullptr;
static lv_obj_t   *s_page   = nullptr;

// Column x positions (content spans UI_SAFE .. UI_W - UI_SAFE)
static const int X_NAME = UI_SAFE;
static const int X_RAIL = UI_SAFE + 92;
static const int X_WARM = UI_SAFE + 170;
static const int X_COOL = UI_SAFE + 270;
static const int X_HS   = UI_SAFE + 375;

static lv_obj_t *mk_label(lv_obj_t *parent, int x, int y, const lv_font_t *font,
                          lv_color_t col, const char *txt, int w = 0) {
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(l, col, LV_PART_MAIN);
    if (w > 0) { lv_label_set_long_mode(l, LV_LABEL_LONG_DOT); lv_obj_set_width(l, w); }
    lv_obj_set_pos(l, x, y);
    return l;
}

static void fmt_ch(char *buf, size_t len, const SenseReading &r, uint8_t p, uint8_t ch) {
    int duty = (int)(pwm_duty_frac(p, ch) * 100.0f + 0.5f);
    snprintf(buf, len, "%.3f A  %d%%", r.amps[ch], duty);
}

static void refresh(lv_timer_t *t) {
    char buf[240];
    int online = 0, present = 0;

    for (uint8_t p = 0; p < 3; p++) {
        DiagRow &row = s_rows[p];
        bool pres = sense_present(p);
        bool on   = sense_online(p);
        present += pres;
        online  += on;
        const SenseReading &r = sense_get(p);

        if (on) {
            snprintf(buf, sizeof(buf), "%.1f V", r.rail_v);     lv_label_set_text(row.rail, buf);
            fmt_ch(buf, sizeof(buf), r, p, 0);                   lv_label_set_text(row.warm, buf);
            fmt_ch(buf, sizeof(buf), r, p, 1);                   lv_label_set_text(row.cool, buf);
        } else {
            lv_label_set_text(row.rail, "--");
            lv_label_set_text(row.warm, "--");
            lv_label_set_text(row.cool, "--");
        }
        bool hs = pwm_hs_on(p);
        lv_label_set_text(row.hs, hs ? "ON" : "off");
        lv_obj_set_style_text_color(row.hs, hs ? UI_COL_GOOD : UI_COL_TEXT_DIM, LV_PART_MAIN);

        // Status line
        uint16_t f = fault_flags(p);
        lv_color_t col;
        char desc[192];
        fault_describe(p, desc, sizeof(desc));
        for (char *c = desc; *c; c++) if (*c == '\n') *c = ';';

        if (fault_tripped(p)) {
            snprintf(buf, sizeof(buf), "TRIPPED: %s", desc);
            col = UI_COL_OFF_LIT;
        } else if (!g_state.port_active[p]) {
            snprintf(buf, sizeof(buf), "Port disabled in Port Configuration");
            col = UI_COL_TEXT_DIM;
        } else if (!pres) {
            snprintf(buf, sizeof(buf), "ADC not found at 0x%02X", 0x48 + p);
            col = UI_COL_WARN;
        } else if (f & FW_WARN_MASK) {
            snprintf(buf, sizeof(buf), "%s", desc);
            col = UI_COL_WARN;
        } else {
            snprintf(buf, sizeof(buf), "OK");
            col = UI_COL_GOOD;
        }
        lv_label_set_text(row.status, buf);
        lv_obj_set_style_text_color(row.status, col, LV_PART_MAIN);
    }

    if (fault_no_supply()) snprintf(buf, sizeof(buf), "No 24V at boot");
    else                   snprintf(buf, sizeof(buf), "ADCs %d/3 online", online);
    lv_label_set_text(s_footer, buf);
    lv_obj_set_style_text_color(s_footer,
        (fault_no_supply() || online < present) ? UI_COL_WARN : UI_COL_TEXT_DIM, LV_PART_MAIN);

    if (fault_any_trip()) lv_obj_clear_state(s_reset, LV_STATE_DISABLED);
    else                  lv_obj_add_state(s_reset, LV_STATE_DISABLED);
}

static void reset_cb(lv_event_t *e) {
    fault_ui_reset_all();
    refresh(nullptr);
}

static void page_deleted_cb(lv_event_t *e) {
    if (s_timer) { lv_timer_del(s_timer); s_timer = nullptr; }
    s_footer = nullptr;
    s_reset  = nullptr;
    s_page   = nullptr;
}

// ── Footer buttons ──────────────────────────────────────────
static lv_obj_t *mk_foot_btn(lv_obj_t *parent, int x, int y, int w, const char *txt) {
    lv_obj_t *b = lv_btn_create(parent);
    lv_obj_set_size(b, w, 38);
    lv_obj_set_pos(b, x, y);
    ui_style_outline_btn(b);
    lv_obj_set_style_pad_all(b, 0, LV_PART_MAIN);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, UI_FONT_BODY, LV_PART_MAIN);
    lv_obj_set_style_text_color(l, UI_COL_TEXT, LV_PART_MAIN);
    lv_obj_center(l);
    return b;
}

// ── Child views (full-screen, over the live readout) ────────
static lv_obj_t *mk_view(const char *title, lv_event_cb_t back_cb) {
    lv_obj_t *c = lv_obj_create(s_page);
    lv_obj_set_size(c, UI_W, UI_H);
    lv_obj_set_pos(c, 0, 0);
    lv_obj_set_style_bg_color(c, UI_COL_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(c, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(c, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(c, 0, LV_PART_MAIN);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *bar = lv_obj_create(c);
    lv_obj_set_size(bar, UI_W, UI_BAR_H);
    lv_obj_set_pos(bar, 0, 0);
    ui_style_bar(bar);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
    lv_obj_t *t = lv_label_create(bar);
    lv_label_set_text(t, title);
    lv_obj_set_style_text_color(t, UI_COL_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(t, UI_FONT_LABEL, LV_PART_MAIN);
    lv_obj_align(t, LV_ALIGN_LEFT_MID, UI_SAFE, 0);
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
    lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, c);
    return c;
}

static void view_back_cb(lv_event_t *e) {
    lv_obj_del_async((lv_obj_t *)lv_event_get_user_data(e));
}

// ── Panel Check view ────────────────────────────────────────
//   Panel     Warm                 Cool                 Rail
//   Panel A   0.097 A  PASS        0.099 A  PASS        24.0 V
//             ref 0.096 A          ref 0.096 A
static lv_obj_t   *s_pc_warm[3], *s_pc_cool[3], *s_pc_rail[3];
static lv_obj_t   *s_pc_wref[3], *s_pc_cref[3];
static lv_obj_t   *s_pc_status = nullptr, *s_pc_btn_lbl = nullptr, *s_pc_save = nullptr;
static lv_timer_t *s_pc_timer = nullptr;
static uint8_t     s_pc_saved = 0;    // ports whose reference was just saved

static lv_color_t verdict_col(PcVerdict v) {
    switch (v) {
        case PC_PASS:    return UI_COL_GOOD;
        case PC_NOREF:   return UI_COL_TEXT;
        case PC_NOT_RUN:
        case PC_SKIPPED: return UI_COL_TEXT_DIM;
        default:         return UI_COL_OFF_LIT;
    }
}

static void ref_text(char *b, size_t n, uint8_t p, uint8_t ch) {
    float e = panel_check_expected_a(p, ch);
    if (e > 0) snprintf(b, n, "ref %.3f A", e);
    else       snprintf(b, n, "no reference");
}

static void pc_refresh(lv_timer_t *t) {
    if (!s_pc_status) return;
    char b[64];
    bool run = panel_check_running();
    int cur_p = panel_check_current_panel(), cur_c = panel_check_current_ch();
    int issues = 0, tested = 0, noref = 0;
    for (uint8_t p = 0; p < 3; p++) {
        const PcPanel &r = panel_check_result(p);
        lv_obj_t *cell[2] = { s_pc_warm[p], s_pc_cool[p] };
        lv_obj_t *refl[2] = { s_pc_wref[p], s_pc_cref[p] };
        bool skipped = r.skip_why && !r.tested;
        // A skip reason spans the (empty) cool cell; results use the normal width.
        lv_obj_set_width(cell[0], skipped ? 260 : 132);
        for (uint8_t ch = 0; ch < 2; ch++) {
            const PcChannel &c = r.ch[ch];
            ref_text(b, sizeof(b), p, ch);
            lv_label_set_text(refl[ch], (skipped || !g_state.port_active[p]) ? "" : b);
            if (run && cur_p == p && cur_c == ch) {
                lv_label_set_text(cell[ch], "testing...");
                lv_obj_set_style_text_color(cell[ch], UI_COL_ACCENT, LV_PART_MAIN);
            } else if (skipped) {
                lv_label_set_text(cell[ch], ch == 0 ? r.skip_why : "");
                lv_obj_set_style_text_color(cell[ch], UI_COL_TEXT_DIM, LV_PART_MAIN);
            } else if (c.verdict == PC_NOT_RUN) {
                lv_label_set_text(cell[ch], "--");
                lv_obj_set_style_text_color(cell[ch], UI_COL_TEXT_DIM, LV_PART_MAIN);
            } else {
                bool show_a = c.verdict == PC_PASS || c.verdict == PC_LOW ||
                              c.verdict == PC_HIGH || c.verdict == PC_NOREF;
                if (show_a) snprintf(b, sizeof(b), "%.3f A  %s", c.amps, panel_check_verdict_str(c.verdict));
                else        snprintf(b, sizeof(b), "%s", panel_check_verdict_str(c.verdict));
                lv_label_set_text(cell[ch], b);
                lv_obj_set_style_text_color(cell[ch], verdict_col(c.verdict), LV_PART_MAIN);
                if (c.verdict == PC_NOREF)      noref++;
                else if (c.verdict != PC_PASS)  issues++;
                tested++;
            }
        }
        if (r.tested && r.rail_v > 0) { snprintf(b, sizeof(b), "%.1f V", r.rail_v); lv_label_set_text(s_pc_rail[p], b); }
        else lv_label_set_text(s_pc_rail[p], "--");
    }

    lv_color_t sc = UI_COL_TEXT_DIM;
    if (run)                                   { lv_label_set_text(s_pc_status, "Running: warm, then cool"); sc = UI_COL_ACCENT; }
    else if (s_pc_saved)                       { snprintf(b, sizeof(b), "Reference saved:%s%s%s",
                                                     (s_pc_saved & 1) ? " A" : "", (s_pc_saved & 2) ? " B" : "",
                                                     (s_pc_saved & 4) ? " C" : "");
                                                 lv_label_set_text(s_pc_status, b); sc = UI_COL_GOOD; }
    else if (panel_check_done() && !tested)    lv_label_set_text(s_pc_status, "Nothing tested");
    else if (panel_check_done() && issues)     { snprintf(b, sizeof(b), "%d channel(s) need attention", issues);
                                                 lv_label_set_text(s_pc_status, b); sc = UI_COL_WARN; }
    else if (panel_check_done() && noref)      lv_label_set_text(s_pc_status, "Good panel? Save reference");
    else if (panel_check_done())               { lv_label_set_text(s_pc_status, "All channels passed"); sc = UI_COL_GOOD; }
    else                                       lv_label_set_text(s_pc_status, "Panels return to their settings after");
    lv_obj_set_style_text_color(s_pc_status, sc, LV_PART_MAIN);
    lv_label_set_text(s_pc_btn_lbl, run ? "Cancel" : "Start");
    if (!run && panel_check_can_save()) lv_obj_clear_state(s_pc_save, LV_STATE_DISABLED);
    else                                lv_obj_add_state(s_pc_save, LV_STATE_DISABLED);
}

static void pc_deleted_cb(lv_event_t *e) {
    panel_check_cancel();              // leaving mid-test restores the panels
    if (s_pc_timer) { lv_timer_del(s_pc_timer); s_pc_timer = nullptr; }
    s_pc_status = s_pc_btn_lbl = s_pc_save = nullptr;
}

static void open_check_view() {
    lv_obj_t *c = mk_view("Panel Check", view_back_cb);
    lv_obj_add_event_cb(c, pc_deleted_cb, LV_EVENT_DELETE, nullptr);
    s_pc_saved = 0;

    char b[160];
    snprintf(b, sizeof(b), "Lights each channel alone at %d%% and compares it with that panel's "
             "saved reference (pass within %d%%).",
             (int)(PANELCHECK_DUTY * 100 + 0.5f), (int)(PANELCHECK_REF_TOL * 100 + 0.5f));
    lv_obj_t *intro = mk_label(c, UI_SAFE, UI_BAR_H + 6, UI_FONT_SMALL, UI_COL_TEXT_DIM, b, UI_W - 2 * UI_SAFE);
    lv_label_set_long_mode(intro, LV_LABEL_LONG_WRAP);

    const int HY = UI_BAR_H + 44;
    const int RP = 46;                                       // row pitch
    const int CX_W = UI_SAFE + 100, CX_C = UI_SAFE + 236, CX_R = UI_SAFE + 372;
    mk_label(c, X_NAME, HY, UI_FONT_SMALL, UI_COL_TEXT_DIM, "Panel");
    mk_label(c, CX_W,   HY, UI_FONT_SMALL, UI_COL_TEXT_DIM, "Warm");
    mk_label(c, CX_C,   HY, UI_FONT_SMALL, UI_COL_TEXT_DIM, "Cool");
    mk_label(c, CX_R,   HY, UI_FONT_SMALL, UI_COL_TEXT_DIM, "Rail");
    for (uint8_t p = 0; p < 3; p++) {
        int y = HY + 18 + p * RP;
        lv_obj_t *bg = lv_obj_create(c);
        lv_obj_set_size(bg, UI_W - 2 * UI_SAFE + 12, RP - 4);
        lv_obj_set_pos(bg, UI_SAFE - 6, y);
        ui_style_card(bg);
        lv_obj_set_style_pad_all(bg, 0, LV_PART_MAIN);
        lv_obj_clear_flag(bg, LV_OBJ_FLAG_CLICKABLE);
        mk_label(c, X_NAME, y + 12, UI_FONT_BODY, UI_COL_ACCENT, g_state.name[p], CX_W - X_NAME - 6);
        s_pc_warm[p] = mk_label(c, CX_W, y + 4,  UI_FONT_BODY,  UI_COL_TEXT_DIM, "--", CX_C - CX_W - 4);
        s_pc_cool[p] = mk_label(c, CX_C, y + 4,  UI_FONT_BODY,  UI_COL_TEXT_DIM, "--", CX_R - CX_C - 4);
        s_pc_wref[p] = mk_label(c, CX_W, y + 23, UI_FONT_SMALL, UI_COL_TEXT_DIM, "", CX_C - CX_W - 4);
        s_pc_cref[p] = mk_label(c, CX_C, y + 23, UI_FONT_SMALL, UI_COL_TEXT_DIM, "", CX_R - CX_C - 4);
        s_pc_rail[p] = mk_label(c, CX_R, y + 12, UI_FONT_BODY,  UI_COL_TEXT, "--");
    }

    const int FY = UI_H - 46 + 2;
    s_pc_status = mk_label(c, UI_SAFE, FY + 12, UI_FONT_SMALL, UI_COL_TEXT_DIM, "",
                           UI_W - 2 * UI_SAFE - 100 - 8 - 140 - 6);

    s_pc_save = mk_foot_btn(c, UI_W - UI_SAFE - 100 - 8 - 140, FY, 140, "Save reference");
    lv_obj_set_style_bg_color(s_pc_save, UI_COL_PANEL_BG, LV_PART_MAIN | LV_STATE_DISABLED);
    lv_obj_set_style_border_color(s_pc_save, UI_COL_DIM, LV_PART_MAIN | LV_STATE_DISABLED);
    lv_obj_set_style_text_color(lv_obj_get_child(s_pc_save, 0), UI_COL_TEXT_DIM, LV_PART_MAIN | LV_STATE_DISABLED);
    lv_obj_add_event_cb(s_pc_save, [](lv_event_t *e) {
        s_pc_saved = panel_check_save_reference();
        if (s_pc_saved) schedule_save();
        pc_refresh(nullptr);
    }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *go = mk_foot_btn(c, UI_W - UI_SAFE - 100, FY, 100, "Start");
    s_pc_btn_lbl = lv_obj_get_child(go, 0);
    lv_obj_set_style_bg_color(go, UI_COL_ACCENT_DIM, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(go, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(go, UI_COL_ACCENT, LV_PART_MAIN);
    lv_obj_add_event_cb(go, [](lv_event_t *e) {
        s_pc_saved = 0;
        if (panel_check_running()) panel_check_cancel(); else panel_check_start();
        pc_refresh(nullptr);
    }, LV_EVENT_CLICKED, nullptr);

    pc_refresh(nullptr);
    if (s_pc_timer) lv_timer_del(s_pc_timer);
    s_pc_timer = lv_timer_create(pc_refresh, 200, nullptr);
}

// ── System view ─────────────────────────────────────────────
static lv_obj_t   *s_sy_val[8];
static lv_obj_t   *s_sy_peak[3];
static lv_timer_t *s_sy_timer = nullptr;

static void sy_refresh(lv_timer_t *t) {
    if (!s_sy_val[0]) return;
    char b[96], ago[32];
    sysinfo_uptime_str(b, sizeof(b));                           lv_label_set_text(s_sy_val[0], b);
    lv_label_set_text(s_sy_val[1], sysinfo_reset_reason());
    lv_obj_set_style_text_color(s_sy_val[1], sysinfo_reset_abnormal() ? UI_COL_WARN : UI_COL_TEXT, LV_PART_MAIN);
    uint8_t lp; uint16_t lb; uint32_t lm;
    if (fault_last_event(&lp, &lb, &lm)) {
        sysinfo_ago_str(lm, ago, sizeof(ago));
        lv_label_set_text(s_sy_val[7], ago);
        snprintf(b, sizeof(b), "%s: %s", g_state.name[lp], fault_bit_name(lb));
        lv_obj_set_style_text_color(s_sy_val[2], (lb & FT_TRIP_MASK) ? UI_COL_OFF_LIT : UI_COL_WARN, LV_PART_MAIN);
    } else {
        snprintf(b, sizeof(b), "None since boot");
        lv_label_set_text(s_sy_val[7], "--");
        lv_obj_set_style_text_color(s_sy_val[2], UI_COL_GOOD, LV_PART_MAIN);
    }
    lv_label_set_text(s_sy_val[2], b);
    snprintf(b, sizeof(b), "%lu", (unsigned long)fault_trip_count());          lv_label_set_text(s_sy_val[3], b);
    snprintf(b, sizeof(b), "ADC %lu   touch %lu", (unsigned long)sense_i2c_errors(),
             (unsigned long)touch_i2c_errors());                                lv_label_set_text(s_sy_val[4], b);
    snprintf(b, sizeof(b), "%lu", (unsigned long)sense_sweeps());                                lv_label_set_text(s_sy_val[5], b);
    snprintf(b, sizeof(b), "%lu kHz, %s curve", (unsigned long)(pwm_freq_hz() / 1000),
             pwm_curve_perceptual() ? "perceptual" : "linear");                lv_label_set_text(s_sy_val[6], b);
    for (uint8_t p = 0; p < 3; p++) {
        snprintf(b, sizeof(b), "%-10.10s  warm %.3f A   cool %.3f A", g_state.name[p],
                 sense_peak(p, 0), sense_peak(p, 1));
        lv_label_set_text(s_sy_peak[p], b);
    }
}

static void sy_deleted_cb(lv_event_t *e) {
    if (s_sy_timer) { lv_timer_del(s_sy_timer); s_sy_timer = nullptr; }
    s_sy_val[0] = nullptr;
}

static void open_sys_view() {
    lv_obj_t *c = mk_view("System", view_back_cb);
    lv_obj_add_event_cb(c, sy_deleted_cb, LV_EVENT_DELETE, nullptr);
    static const char *K[8] = { "Uptime", "Last reset", "Last fault", "Trips since boot",
                                "I2C errors", "Sense sweeps", "Panel PWM", "When" };
    static const int ORDER[8] = { 0, 1, 2, 7, 3, 4, 5, 6 };   // "When" shown under "Last fault"
    const int KX = UI_SAFE, VX = UI_SAFE + 128, RH = 20;
    int y = UI_BAR_H + 8;
    for (int r = 0; r < 8; r++, y += RH) {
        int i = ORDER[r];
        mk_label(c, KX, y, UI_FONT_SMALL, UI_COL_TEXT_DIM, K[i]);
        s_sy_val[i] = mk_label(c, VX, y - 1, UI_FONT_BODY, UI_COL_TEXT, "", UI_W - UI_SAFE - VX);
    }
    y += 6;
    mk_label(c, KX, y, UI_FONT_SMALL, UI_COL_TEXT_DIM, "Peak current since boot");
    y += 18;
    for (uint8_t p = 0; p < 3; p++, y += 18)
        s_sy_peak[p] = mk_label(c, KX, y, UI_FONT_SMALL, UI_COL_TEXT, "", UI_W - 2 * UI_SAFE - 130);
    lv_obj_t *clr = mk_foot_btn(c, UI_W - UI_SAFE - 120, UI_H - 46 + 2, 120, "Clear peaks");
    lv_obj_add_event_cb(clr, [](lv_event_t *e) { sense_peak_reset(); sy_refresh(nullptr); }, LV_EVENT_CLICKED, nullptr);

    sy_refresh(nullptr);
    if (s_sy_timer) lv_timer_del(s_sy_timer);
    s_sy_timer = lv_timer_create(sy_refresh, 500, nullptr);
}

lv_obj_t *diag_page_create(lv_obj_t *parent, lv_event_cb_t back_cb) {
    lv_obj_t *v = lv_obj_create(parent);
    lv_obj_set_size(v, UI_W, UI_H);
    lv_obj_set_pos(v, 0, 0);
    lv_obj_set_style_bg_color(v, UI_COL_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(v, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(v, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(v, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(v, 0, LV_PART_MAIN);
    lv_obj_clear_flag(v, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(v, page_deleted_cb, LV_EVENT_DELETE, nullptr);
    s_page = v;

    // ── Header bar (same look as the other Menu sub-pages) ──
    lv_obj_t *bar = lv_obj_create(v);
    lv_obj_set_size(bar, UI_W, UI_BAR_H);
    lv_obj_set_pos(bar, 0, 0);
    ui_style_bar(bar);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
    lv_obj_t *title = lv_label_create(bar);
    lv_label_set_text(title, "Diagnostics");
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

    // ── Vertical budget ─────────────────────────────────────
    const int FOOT_H = 46;                                   // footer button row
    const int HEAD_Y = UI_BAR_H + 6;
    const int HEAD_H = 18;
    const int rows_top = HEAD_Y + HEAD_H;
    const int ROW_H  = (UI_H - FOOT_H - rows_top) / 3;      // ~73 px @40 bar, ~64 @54

    lv_color_t hc = UI_COL_TEXT_DIM;
    mk_label(v, X_NAME, HEAD_Y, UI_FONT_SMALL, hc, "Panel");
    mk_label(v, X_RAIL, HEAD_Y, UI_FONT_SMALL, hc, "Rail");
    mk_label(v, X_WARM, HEAD_Y, UI_FONT_SMALL, hc, "Warm");
    mk_label(v, X_COOL, HEAD_Y, UI_FONT_SMALL, hc, "Cool");
    mk_label(v, X_HS,   HEAD_Y, UI_FONT_SMALL, hc, "24V sw");

    for (uint8_t p = 0; p < 3; p++) {
        int y = rows_top + p * ROW_H;

        lv_obj_t *bg = lv_obj_create(v);                     // zebra band
        lv_obj_set_size(bg, UI_W - 2 * UI_SAFE + 12, ROW_H - 4);
        lv_obj_set_pos(bg, UI_SAFE - 6, y + 2);
        ui_style_card(bg);
        lv_obj_set_style_pad_all(bg, 0, LV_PART_MAIN);
        lv_obj_clear_flag(bg, LV_OBJ_FLAG_CLICKABLE);

        int vy = y + ROW_H / 2 - 18;
        mk_label(v, X_NAME, vy, UI_FONT_BODY, UI_COL_ACCENT, g_state.name[p], X_RAIL - X_NAME - 6);
        DiagRow &row = s_rows[p];
        row.rail   = mk_label(v, X_RAIL, vy, UI_FONT_BODY, UI_COL_TEXT, "--");
        row.warm   = mk_label(v, X_WARM, vy, UI_FONT_BODY, lv_color_hex(0xE0B070), "--");
        row.cool   = mk_label(v, X_COOL, vy, UI_FONT_BODY, lv_color_hex(0x90B8F0), "--");
        row.hs     = mk_label(v, X_HS,   vy, UI_FONT_BODY, UI_COL_TEXT_DIM, "off");
        row.status = mk_label(v, X_NAME, vy + 20, UI_FONT_SMALL, UI_COL_TEXT_DIM, "",
                              UI_W - 2 * UI_SAFE);
    }

    // ── Footer ──────────────────────────────────────────────
    const int FB_W = 92, FB_GAP = 8;
    s_footer = mk_label(v, UI_SAFE, UI_H - FOOT_H + 14, UI_FONT_SMALL, UI_COL_TEXT_DIM, "",
                        UI_W - 2 * UI_SAFE - 3 * FB_W - 3 * FB_GAP);

    lv_obj_t *chk = mk_foot_btn(v, UI_W - UI_SAFE - 3 * FB_W - 2 * FB_GAP, UI_H - FOOT_H + 2, FB_W, "Check");
    lv_obj_add_event_cb(chk, [](lv_event_t *e) { open_check_view(); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *sys = mk_foot_btn(v, UI_W - UI_SAFE - 2 * FB_W - FB_GAP, UI_H - FOOT_H + 2, FB_W, "System");
    lv_obj_add_event_cb(sys, [](lv_event_t *e) { open_sys_view(); }, LV_EVENT_CLICKED, nullptr);

    s_reset = lv_btn_create(v);
    lv_obj_set_size(s_reset, FB_W, 38);
    lv_obj_set_pos(s_reset, UI_W - UI_SAFE - FB_W, UI_H - FOOT_H + 2);
    lv_obj_set_style_bg_color(s_reset, UI_COL_ACCENT_DIM, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_reset, UI_COL_ACCENT, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_reset, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(s_reset, 6, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_reset, UI_COL_PANEL_BG, LV_PART_MAIN | LV_STATE_DISABLED);
    lv_obj_set_style_border_color(s_reset, UI_COL_DIM, LV_PART_MAIN | LV_STATE_DISABLED);
    lv_obj_t *rl = lv_label_create(s_reset);
    lv_label_set_text(rl, "Reset");
    lv_obj_set_style_text_font(rl, UI_FONT_LABEL, LV_PART_MAIN);
    lv_obj_set_style_text_color(rl, UI_COL_WHITE, LV_PART_MAIN);
    lv_obj_center(rl);
    lv_obj_add_event_cb(s_reset, reset_cb, LV_EVENT_CLICKED, nullptr);

    refresh(nullptr);
    if (s_timer) lv_timer_del(s_timer);
    s_timer = lv_timer_create(refresh, 250, nullptr);
    return v;
}
