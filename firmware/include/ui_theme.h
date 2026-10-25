// SPDX-FileCopyrightText: 2026 DrEvan
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
// Shared UI palette, fonts, layout constants and widget style helpers.
#include <lvgl.h>

// ── Palette ──────────────────────────────────────────────────
#define UI_COL_BG           lv_color_hex(0x0A0A10)
#define UI_COL_ACCENT       lv_color_hex(0x3878C8)
#define UI_COL_ACCENT_DIM   lv_color_hex(0x1C3F6E)
#define UI_COL_TEXT         lv_color_hex(0xCCCCCC)
#define UI_COL_TEXT_DIM     lv_color_hex(0x888888)
#define UI_COL_DIM          lv_color_hex(0x333344)
#define UI_COL_PANEL_BG     lv_color_hex(0x12121E)
#define UI_COL_PANEL_BDR    lv_color_hex(0x2A3A58)
#define UI_COL_GOOD         lv_color_hex(0x22CC66)
#define UI_COL_WARN         lv_color_hex(0xEE8822)
#define UI_COL_OFF          lv_color_hex(0x442222)
#define UI_COL_OFF_LIT      lv_color_hex(0xBB2222)
#define UI_COL_WHITE        lv_color_white()
#define UI_COL_BLACK        lv_color_black()

// ── Fonts ─────────────────────────────────────────────────────
#define UI_FONT_SMALL   (&lv_font_montserrat_12)
#define UI_FONT_BODY    (&lv_font_montserrat_14)
#define UI_FONT_LABEL   (&lv_font_montserrat_16)
#define UI_FONT_VALUE   (&lv_font_montserrat_20)
#define UI_FONT_LARGE   (&lv_font_montserrat_32)
#define UI_FONT_TITLE   (&lv_font_montserrat_48)

// ── Layout ────────────────────────────────────────────────────
#define UI_W            480
#define UI_H            320
#define UI_BAR_H        46          // nav bar height (top and bottom)
#define UI_TOPBAR_H     UI_BAR_H
#define UI_BOTBAR_H     UI_BAR_H
#define UI_SAFE         20          // safe inset from screen edge (~3mm @ 165PPI)
#define UI_PAD          8
#define UI_BAR_BTN_H    36          // bar button height (minimum touch target)
#define UI_HIT_PAD      6           // extra touch margin on edge buttons
// Extend an object's touch area beyond its drawn bounds. FT6336 accuracy is
// worst near the screen edges, where the bar buttons sit.
#define ui_hit_pad(obj, px) lv_obj_set_ext_click_area((obj), (px))
#define UI_CONTENT_Y    UI_BAR_H
#define UI_CONTENT_H    (UI_H - 2 * UI_BAR_H)   // 228px

// ── Style helpers ─────────────────────────────────────────────

// Standard slider: pill track and indicator, circular knob.
// Size the slider (width, 10); the 20px knob overhangs the track by 5px,
// so the parent must have LV_OBJ_FLAG_OVERFLOW_VISIBLE.
static inline void ui_style_slider(lv_obj_t *sl) {
    lv_obj_set_style_bg_color(sl, lv_color_hex(0x161626), LV_PART_MAIN);
    lv_obj_set_style_bg_opa (sl, LV_OPA_COVER,            LV_PART_MAIN);
    lv_obj_set_style_radius  (sl, LV_RADIUS_CIRCLE,       LV_PART_MAIN);
    lv_obj_set_style_bg_color(sl, UI_COL_ACCENT,          LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa (sl, LV_OPA_COVER,            LV_PART_INDICATOR);
    lv_obj_set_style_radius  (sl, LV_RADIUS_CIRCLE,       LV_PART_INDICATOR);
    // pad=5 on a 10px track gives a 20px knob
    lv_obj_set_style_bg_color    (sl, lv_color_hex(0xC8D8F0), LV_PART_KNOB);
    lv_obj_set_style_bg_opa      (sl, LV_OPA_COVER,           LV_PART_KNOB);
    lv_obj_set_style_border_color(sl, lv_color_hex(0x3868B0), LV_PART_KNOB);
    lv_obj_set_style_border_width(sl, 2,                       LV_PART_KNOB);
    lv_obj_set_style_pad_all     (sl, 5,                       LV_PART_KNOB);
    lv_obj_set_style_radius      (sl, LV_RADIUS_CIRCLE,        LV_PART_KNOB);
}

// Card / panel background
static inline void ui_style_card(lv_obj_t *obj) {
    lv_obj_set_style_bg_color   (obj, UI_COL_PANEL_BG,  LV_PART_MAIN);
    lv_obj_set_style_bg_opa     (obj, LV_OPA_COVER,     LV_PART_MAIN);
    lv_obj_set_style_border_color(obj, UI_COL_PANEL_BDR, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 1,               LV_PART_MAIN);
    lv_obj_set_style_radius     (obj, 6,                LV_PART_MAIN);
    lv_obj_set_style_pad_all    (obj, UI_PAD,           LV_PART_MAIN);
    lv_obj_clear_flag           (obj, LV_OBJ_FLAG_SCROLLABLE);
}

// Preset-style accent button
static inline void ui_style_preset_btn(lv_obj_t *btn) {
    lv_obj_set_style_bg_color    (btn, UI_COL_ACCENT_DIM, LV_PART_MAIN);
    lv_obj_set_style_bg_opa      (btn, LV_OPA_COVER,      LV_PART_MAIN);
    lv_obj_set_style_border_color(btn, UI_COL_ACCENT,     LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 1,                 LV_PART_MAIN);
    lv_obj_set_style_radius      (btn, 4,                 LV_PART_MAIN);
    lv_obj_set_style_pad_ver     (btn, 4,                 LV_PART_MAIN);
    lv_obj_set_style_pad_hor     (btn, 6,                 LV_PART_MAIN);
    lv_obj_set_style_bg_color    (btn, UI_COL_ACCENT,     LV_STATE_PRESSED | LV_PART_MAIN);
}

// Outline button (for nav / secondary actions)
static inline void ui_style_outline_btn(lv_obj_t *btn) {
    lv_obj_set_style_bg_opa      (btn, LV_OPA_TRANSP,    LV_PART_MAIN);
    lv_obj_set_style_border_color(btn, UI_COL_PANEL_BDR, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 1,                LV_PART_MAIN);
    lv_obj_set_style_radius      (btn, 4,                LV_PART_MAIN);
    lv_obj_set_style_pad_ver     (btn, 4,                LV_PART_MAIN);
    lv_obj_set_style_pad_hor     (btn, 6,                LV_PART_MAIN);
    lv_obj_set_style_bg_color    (btn, UI_COL_DIM,       LV_STATE_PRESSED | LV_PART_MAIN);
    lv_obj_set_style_bg_opa      (btn, LV_OPA_COVER,     LV_STATE_PRESSED | LV_PART_MAIN);
}

// Nav bar (top or bottom)
static inline void ui_style_bar(lv_obj_t *bar) {
    lv_obj_set_style_bg_color    (bar, UI_COL_PANEL_BG,  LV_PART_MAIN);
    lv_obj_set_style_bg_opa      (bar, LV_OPA_COVER,     LV_PART_MAIN);
    lv_obj_set_style_border_color(bar, UI_COL_PANEL_BDR, LV_PART_MAIN);
    lv_obj_set_style_border_width(bar, 1,                LV_PART_MAIN);
    lv_obj_set_style_radius      (bar, 0,                LV_PART_MAIN);
    lv_obj_set_style_pad_all     (bar, 0,                LV_PART_MAIN);
    lv_obj_clear_flag            (bar, LV_OBJ_FLAG_SCROLLABLE);
}

// ── CCT gradient slider ───────────────────────────────────────
// Replaces the default track and indicator with a fixed full-width
// warm-white-cool gradient; the knob moves over it without changing the fill.
// Parent must have LV_OBJ_FLAG_OVERFLOW_VISIBLE.

static void _ui_cct_draw_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_draw_part_dsc_t *dsc = lv_event_get_draw_part_dsc(e);
    if (!dsc) return;

    // Suppress default track and indicator
    if (code == LV_EVENT_DRAW_PART_BEGIN) {
        if (dsc->rect_dsc &&
            (dsc->part == LV_PART_MAIN || dsc->part == LV_PART_INDICATOR))
            dsc->rect_dsc->bg_opa = LV_OPA_TRANSP;
        return;
    }

    // After MAIN: draw the gradient as two 2-stop segments
    if (code != LV_EVENT_DRAW_PART_END || dsc->part != LV_PART_MAIN) return;

    const lv_area_t *a  = dsc->draw_area;
    lv_draw_ctx_t   *dc = dsc->draw_ctx;
    if (!a || !dc) return;

    lv_coord_t mid = a->x1 + (a->x2 - a->x1) / 2;
    lv_coord_t r   = (a->y2 - a->y1) / 2;   // pill radius = half track height

    lv_draw_rect_dsc_t rd;
    lv_draw_rect_dsc_init(&rd);
    rd.radius       = r;
    rd.bg_opa       = LV_OPA_COVER;
    rd.border_width = 0;
    rd.shadow_width = 0;

    rd.bg_grad.dir         = LV_GRAD_DIR_HOR;
    rd.bg_grad.stops_count = 2;
    rd.bg_grad.dither      = LV_DITHER_NONE;

    // Left half, warm to near-white. Extends r px past mid so its rounded
    // end is hidden under the right half.
    rd.bg_color = lv_color_hex(0xFF9A3C);
    rd.bg_grad.stops[0].color = lv_color_hex(0xFF9A3C);
    rd.bg_grad.stops[0].frac  = 0;
    rd.bg_grad.stops[1].color = lv_color_hex(0xFFFAE8);
    rd.bg_grad.stops[1].frac  = 255;
    lv_area_t la = { a->x1, a->y1, (lv_coord_t)(mid + r), a->y2 };
    lv_draw_rect(dc, &rd, &la);

    // Right half, near-white to cool. Both halves share #FFFAE8 at the
    // join so the overlap is seamless.
    rd.bg_grad.stops[0].color = lv_color_hex(0xFFFAE8);
    rd.bg_grad.stops[1].color = lv_color_hex(0x90C8FF);
    lv_area_t ra = { (lv_coord_t)(mid - r), a->y1, a->x2, a->y2 };
    lv_draw_rect(dc, &rd, &ra);
}

// Call instead of ui_style_slider() for CCT sliders.
static inline void ui_style_cct_slider(lv_obj_t *sl) {
    ui_style_slider(sl);
    lv_obj_add_event_cb(sl, _ui_cct_draw_cb, LV_EVENT_DRAW_PART_BEGIN, nullptr);
    lv_obj_add_event_cb(sl, _ui_cct_draw_cb, LV_EVENT_DRAW_PART_END,   nullptr);
}

// Legacy filled/outline button style
static inline void ui_style_btn(lv_obj_t *btn, bool filled) {
    if (filled) {
        lv_obj_set_style_bg_color    (btn, UI_COL_ACCENT, LV_PART_MAIN);
        lv_obj_set_style_bg_opa      (btn, LV_OPA_COVER,  LV_PART_MAIN);
        lv_obj_set_style_border_width(btn, 0,             LV_PART_MAIN);
    } else {
        lv_obj_set_style_bg_opa      (btn, LV_OPA_TRANSP,    LV_PART_MAIN);
        lv_obj_set_style_border_color(btn, UI_COL_ACCENT,    LV_PART_MAIN);
        lv_obj_set_style_border_width(btn, 1,                LV_PART_MAIN);
    }
    lv_obj_set_style_radius (btn, 4,  LV_PART_MAIN);
    lv_obj_set_style_pad_ver(btn, 4,  LV_PART_MAIN);
    lv_obj_set_style_pad_hor(btn, 10, LV_PART_MAIN);
}
