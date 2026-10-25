// SPDX-FileCopyrightText: 2026 DrEvan
// SPDX-License-Identifier: GPL-3.0-or-later

/*
 * screen_split.cpp - Groups configuration screen (screen_groups_create).
 *
 * One tile per port, coloured by group. Preset buttons: Link All,
 * Separate All, and the three pairings (A+B|C, B+C|A, A+C|B).
 * Changes apply and save immediately.
 */

#include "screens/screen_split.h"
#include "screens/screen_manager.h"
#include "ui_theme.h"
#include "app_state.h"
#include "pwm.h"
#include "save_manager.h"
#include <Arduino.h>

// Tile fill and border colours per group index
static lv_color_t group_tile_col(int g_idx) {
    switch (g_idx % 3) {
        case 0: return lv_color_hex(0x1C3F6E);   // dim accent
        case 1: return lv_color_hex(0x124430);   // dim green
        default: return lv_color_hex(0x4A3010);  // dim amber
    }
}
static lv_color_t group_tile_bdr(int g_idx) {
    switch (g_idx % 3) {
        case 0: return lv_color_hex(0x3878C8);
        case 1: return lv_color_hex(0x22CC66);
        default: return lv_color_hex(0xEE8822);
    }
}

// ── Tile grid ─────────────────────────────────────────────────
static lv_obj_t *s_tiles[APP_MAX_PORTS] = {nullptr, nullptr, nullptr};
static lv_obj_t *s_tile_labels[APP_MAX_PORTS] = {nullptr, nullptr, nullptr};

// ── Grouping buttons ──────────────────────────────────────────
static lv_obj_t *s_btn_link_all  = nullptr;
static lv_obj_t *s_btn_sep_all   = nullptr;
static lv_obj_t *s_btn_pairs[3]  = {nullptr, nullptr, nullptr};

// ── Grouping pattern detection ────────────────────────────────
enum GroupingPattern { GP_NONE, GP_LINK_ALL, GP_SEP_ALL, GP_AB_C, GP_BC_A, GP_AC_B };
static const GroupingPattern PAIR_PATTERNS[3] = { GP_AB_C, GP_BC_A, GP_AC_B };

static int port_group(int port);

static GroupingPattern detect_grouping() {
    int active = 0;
    for (int p = 0; p < APP_MAX_PORTS; p++)
        if (g_state.port_active[p]) active++;

    if (g_state.group_count == 1 && g_state.groups[0].count == active)
        return GP_LINK_ALL;

    if (g_state.group_count == active && active > 0) {
        bool all_sep = true;
        for (int p = 0; p < APP_MAX_PORTS && all_sep; p++) {
            if (!g_state.port_active[p]) continue;
            int g = port_group(p);
            if (g < 0 || g_state.groups[g].count != 1) all_sep = false;
        }
        if (all_sep) return GP_SEP_ALL;
    }

    // Pair patterns only apply with all 3 ports active
    if (active == 3 && g_state.group_count == 2) {
        int g0 = port_group(0), g1 = port_group(1), g2 = port_group(2);
        if (g0 >= 0 && g0 == g1 && g0 != g2) return GP_AB_C;
        if (g1 >= 0 && g1 == g2 && g1 != g0) return GP_BC_A;
        if (g0 >= 0 && g0 == g2 && g0 != g1) return GP_AC_B;
    }

    return GP_NONE;
}

// Filled when active, outline otherwise
static void apply_grp_btn_style(lv_obj_t *btn, bool active) {
    if (!btn) return;
    if (active) {
        lv_obj_set_style_bg_color(btn, UI_COL_ACCENT, LV_PART_MAIN);
        lv_obj_set_style_bg_opa  (btn, LV_OPA_COVER,  LV_PART_MAIN);
        lv_obj_set_style_border_width(btn, 0,          LV_PART_MAIN);
    } else {
        ui_style_outline_btn(btn);
    }
    lv_obj_t *lbl = lv_obj_get_child(btn, 0);
    if (lbl) lv_obj_set_style_text_color(lbl,
        active ? UI_COL_WHITE : UI_COL_TEXT_DIM, LV_PART_MAIN);
}

static void refresh_group_btns() {
    GroupingPattern pat = detect_grouping();
    apply_grp_btn_style(s_btn_link_all, pat == GP_LINK_ALL);
    apply_grp_btn_style(s_btn_sep_all,  pat == GP_SEP_ALL);
    for (int c = 0; c < 3; c++)
        apply_grp_btn_style(s_btn_pairs[c], pat == PAIR_PATTERNS[c]);
}

// Group index containing port, or -1
static int port_group(int port) {
    for (int g = 0; g < g_state.group_count; g++)
        for (int s = 0; s < g_state.groups[g].count; s++)
            if (g_state.groups[g].ports[s] == port) return g;
    return -1;
}

static void refresh_tiles() {
    for (int p = 0; p < APP_MAX_PORTS; p++) {
        if (!s_tiles[p]) continue;
        if (!g_state.port_active[p]) {
            lv_obj_set_style_bg_color(s_tiles[p], UI_COL_DIM, LV_PART_MAIN);
            lv_obj_set_style_border_color(s_tiles[p], UI_COL_PANEL_BDR, LV_PART_MAIN);
            continue;
        }
        int g = port_group(p);
        if (g < 0) g = 0;
        lv_obj_set_style_bg_color(s_tiles[p], group_tile_col(g), LV_PART_MAIN);
        lv_obj_set_style_border_color(s_tiles[p], group_tile_bdr(g), LV_PART_MAIN);
    }
}

// ── Apply grouping ────────────────────────────────────────────
static void apply_and_save() {
    // A group is on if any member was on, so joining a lit panel to a
    // dark one turns both on.
    for (int g = 0; g < g_state.group_count; g++) {
        bool any_on = false;
        for (int s = 0; s < g_state.groups[g].count; s++)
            if (g_state.panel_on[g_state.groups[g].ports[s]]) { any_on = true; break; }
        for (int s = 0; s < g_state.groups[g].count; s++)
            g_state.panel_on[g_state.groups[g].ports[s]] = any_on;
    }

    if (g_state.system_on) {
        for (int p = 0; p < APP_MAX_PORTS; p++) {
            if (!g_state.port_active[p]) continue;
            pwm_set_panel(p,
                g_state.panel_on[p] ? g_state.brightness[p] : 0,
                g_state.cct[p]);
        }
    }
    schedule_save();
}

// ── Button callbacks ──────────────────────────────────────────
static void done_cb(lv_event_t *e) {
    apply_and_save();
    screen_manager_goto(SCR_MAIN);
}

static void link_all_cb(lv_event_t *e) {
    groups_link_all();
    refresh_tiles();
    refresh_group_btns();
    apply_and_save();
}

static void sep_all_cb(lv_event_t *e) {
    groups_separate_all();
    refresh_tiles();
    refresh_group_btns();
    apply_and_save();
}

// p0 + p1 in one group, p2 alone; inactive ports are skipped
static void set_two_groups(int p0, int p1, int p2) {
    g_state.group_count = 0;

    g_state.groups[0].count = 0;
    if (g_state.port_active[p0])
        g_state.groups[0].ports[g_state.groups[0].count++] = p0;
    if (g_state.port_active[p1])
        g_state.groups[0].ports[g_state.groups[0].count++] = p1;
    if (g_state.groups[0].count > 0) g_state.group_count++;

    g_state.groups[1].count = 0;
    if (g_state.port_active[p2]) {
        g_state.groups[1].ports[0] = p2;
        g_state.groups[1].count    = 1;
        g_state.group_count++;
    }

    refresh_tiles();
    refresh_group_btns();
    apply_and_save();
}

// user_data = combo index: 0 = A+B|C, 1 = B+C|A, 2 = A+C|B
static void pair_cb(lv_event_t *e) {
    int combo = (int)(intptr_t)lv_event_get_user_data(e);
    switch (combo) {
        case 0: set_two_groups(0, 1, 2); break;
        case 1: set_two_groups(1, 2, 0); break;
        case 2: set_two_groups(0, 2, 1); break;
    }
    refresh_group_btns();
}

// ── Action button helper ──────────────────────────────────────
static lv_obj_t* make_bar_btn(lv_obj_t *parent, const char *label,
                               int w, lv_event_cb_t cb, void *ud,
                               bool filled) {
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, w, 28);
    ui_hit_pad(btn, 3);
    lv_obj_set_style_radius(btn, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn, 0, LV_PART_MAIN);
    if (filled) {
        lv_obj_set_style_bg_color(btn, UI_COL_ACCENT, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN);
    } else {
        ui_style_outline_btn(btn);
    }
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_color(lbl,
        filled ? UI_COL_WHITE : UI_COL_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl, UI_FONT_SMALL, LV_PART_MAIN);
    lv_obj_center(lbl);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, ud);
    return btn;
}

// ── Public ────────────────────────────────────────────────────
lv_obj_t* screen_groups_create() {
    for (int p = 0; p < APP_MAX_PORTS; p++) {
        s_tiles[p]       = nullptr;
        s_tile_labels[p] = nullptr;
    }
    s_btn_link_all = nullptr;
    s_btn_sep_all  = nullptr;
    for (int c = 0; c < 3; c++) s_btn_pairs[c] = nullptr;

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

    lv_obj_t *lbl_t = lv_label_create(topbar);
    lv_label_set_text(lbl_t, "Groups");
    lv_obj_set_style_text_color(lbl_t, UI_COL_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl_t, UI_FONT_LABEL, LV_PART_MAIN);
    lv_obj_align(lbl_t, LV_ALIGN_LEFT_MID, UI_SAFE, 0);

    lv_obj_t *btn_done = lv_btn_create(topbar);
    lv_obj_set_size(btn_done, 90, UI_BAR_BTN_H);
    ui_hit_pad(btn_done, UI_HIT_PAD);
    lv_obj_align(btn_done, LV_ALIGN_RIGHT_MID, -UI_SAFE, 0);
    lv_obj_set_style_bg_color(btn_done, UI_COL_ACCENT, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn_done, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_done, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(btn_done, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn_done, 0, LV_PART_MAIN);
    lv_obj_t *lbl_done = lv_label_create(btn_done);
    lv_label_set_text(lbl_done, "Done");
    lv_obj_set_style_text_color(lbl_done, UI_COL_WHITE, LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl_done, UI_FONT_SMALL, LV_PART_MAIN);
    lv_obj_center(lbl_done);
    lv_obj_add_event_cb(btn_done, done_cb, LV_EVENT_CLICKED, nullptr);

    // ── Panel tiles ───────────────────────────────────────────
    static const char *port_letters[3] = {"A", "B", "C"};
    int tile_y  = UI_BAR_H + 10;
    int tile_h  = 90;
    int tile_gap = 10;
    int tile_w  = (UI_W - 2 * UI_SAFE - 2 * tile_gap) / 3;

    for (int p = 0; p < APP_MAX_PORTS; p++) {
        int tx = UI_SAFE + p * (tile_w + tile_gap);
        lv_obj_t *tile = lv_obj_create(scr);
        lv_obj_set_size(tile, tile_w, tile_h);
        lv_obj_set_pos(tile, tx, tile_y);
        int g = port_group(p);
        if (g < 0) g = 0;
        lv_obj_set_style_bg_color(tile,
            g_state.port_active[p] ? group_tile_col(g) : UI_COL_DIM,
            LV_PART_MAIN);
        lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(tile,
            g_state.port_active[p] ? group_tile_bdr(g) : UI_COL_PANEL_BDR,
            LV_PART_MAIN);
        lv_obj_set_style_border_width(tile, 2, LV_PART_MAIN);
        lv_obj_set_style_radius(tile, 6, LV_PART_MAIN);
        lv_obj_set_style_pad_all(tile, 6, LV_PART_MAIN);
        lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
        s_tiles[p] = tile;

            lv_obj_t *lbl_port = lv_label_create(tile);
        char port_hdr[4]; lv_snprintf(port_hdr, sizeof(port_hdr), "(%s)", port_letters[p]);
        lv_label_set_text(lbl_port, port_hdr);
        lv_obj_set_style_text_color(lbl_port, UI_COL_TEXT_DIM, LV_PART_MAIN);
        lv_obj_set_style_text_font(lbl_port, UI_FONT_SMALL, LV_PART_MAIN);
        lv_obj_align(lbl_port, LV_ALIGN_TOP_MID, 0, 0);

            lv_obj_t *lbl_nm = lv_label_create(tile);
        lv_label_set_text(lbl_nm,
            g_state.port_active[p] ? g_state.name[p] : "(inactive)");
        lv_label_set_long_mode(lbl_nm, LV_LABEL_LONG_DOT);
        lv_obj_set_width(lbl_nm, tile_w - 14);
        lv_obj_set_style_text_color(lbl_nm,
            g_state.port_active[p] ? UI_COL_TEXT : UI_COL_TEXT_DIM,
            LV_PART_MAIN);
        lv_obj_set_style_text_font(lbl_nm, UI_FONT_BODY, LV_PART_MAIN);
        lv_obj_align(lbl_nm, LV_ALIGN_CENTER, 0, 4);
        s_tile_labels[p] = lbl_nm;

            if (g_state.port_active[p]) {
            lv_obj_t *lbl_g = lv_label_create(tile);
            char gbuf[6]; lv_snprintf(gbuf, sizeof(gbuf), "G%d", g + 1);
            lv_label_set_text(lbl_g, gbuf);
            lv_obj_set_style_text_color(lbl_g, group_tile_bdr(g), LV_PART_MAIN);
            lv_obj_set_style_text_font(lbl_g, UI_FONT_SMALL, LV_PART_MAIN);
            lv_obj_align(lbl_g, LV_ALIGN_BOTTOM_MID, 0, -2);
        }
    }

    // ── Grouping preset buttons ───────────────────────────────
    int btn_y   = tile_y + tile_h + 14;
    int bw_full = UI_W - 2 * UI_SAFE;
    int bw_pair = (bw_full - 2 * 6) / 3;

    // Highlight the button matching the current grouping
    GroupingPattern cur_pat = detect_grouping();

    // Row 1: [Link All]  [Separate All]
    int r1_bw = (bw_full - 6) / 2;
    s_btn_link_all = make_bar_btn(scr, "Link All",     r1_bw, link_all_cb, nullptr,
                                   cur_pat == GP_LINK_ALL);
    lv_obj_set_pos(s_btn_link_all, UI_SAFE, btn_y);
    s_btn_sep_all  = make_bar_btn(scr, "Separate All", r1_bw, sep_all_cb,  nullptr,
                                   cur_pat == GP_SEP_ALL);
    lv_obj_set_pos(s_btn_sep_all, UI_SAFE + r1_bw + 6, btn_y);
    btn_y += 36;

    // Row 2: [A+B | C]  [B+C | A]  [A+C | B]
    const char *pair_labels[3] = {"A+B | C", "B+C | A", "A+C | B"};
    for (int c = 0; c < 3; c++) {
        s_btn_pairs[c] = make_bar_btn(scr, pair_labels[c], bw_pair, pair_cb,
                                      (void*)(intptr_t)c,
                                      cur_pat == PAIR_PATTERNS[c]);
        lv_obj_set_pos(s_btn_pairs[c], UI_SAFE + c * (bw_pair + 6), btn_y);
    }
    btn_y += 36;

    // ── Bottom bar ────────────────────────────────────────────
    lv_obj_t *botbar = lv_obj_create(scr);
    lv_obj_set_size(botbar, UI_W, UI_BAR_H);
    lv_obj_set_pos(botbar, 0, UI_H - UI_BAR_H);
    ui_style_bar(botbar);
    lv_obj_set_style_border_side(botbar, LV_BORDER_SIDE_TOP, LV_PART_MAIN);

    lv_obj_t *lbl_hint = lv_label_create(botbar);
    lv_label_set_text(lbl_hint, "Grouped panels share brightness & CCT controls");
    lv_obj_set_style_text_color(lbl_hint, UI_COL_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl_hint, UI_FONT_SMALL, LV_PART_MAIN);
    lv_obj_align(lbl_hint, LV_ALIGN_CENTER, 0, 0);

    return scr;
}
