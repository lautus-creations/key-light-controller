// SPDX-FileCopyrightText: 2026 DrEvan
// SPDX-License-Identifier: GPL-3.0-or-later

/*
 * screen_snake.cpp - hidden Snake game (Menu > About, hold the title)
 * Key Light Controller
 *
 * Tap above/below the snake's head to turn up/down while moving sideways,
 * left/right of it while moving vertically. The lights keep running as set.
 * Segments are plain lv_obj squares, one per ring-buffer slot (canvas is
 * disabled in lv_conf.h): each step shows the new head's square and hides
 * the old tail's.
 */

#include "screens/screen_snake.h"
#include "ui_theme.h"
#include <Arduino.h>
#include <cstdio>

#define CELL      16
#define GW        (UI_W / CELL)                 // 30
#define GH        ((UI_H - UI_BAR_H) / CELL)    // 17
#define MAX_LEN   120
#define STEP_MS0  170
#define STEP_MIN  80

struct Pt { int8_t x, y; };

static lv_obj_t   *s_root = nullptr, *s_field = nullptr, *s_food = nullptr;
static lv_obj_t   *s_score = nullptr, *s_msg = nullptr;
static lv_obj_t   *s_seg[MAX_LEN];
static Pt          s_body[MAX_LEN];             // ring buffer, s_head = newest
static int         s_head = 0, s_len = 0;
static int8_t      s_dx = 1, s_dy = 0, s_ndx = 1, s_ndy = 0;
static Pt          s_fp;
static int         s_points = 0, s_best = 0;
static bool        s_dead = false;
static lv_timer_t *s_timer = nullptr;

static void place(lv_obj_t *o, Pt p) { lv_obj_set_pos(o, p.x * CELL + 1, p.y * CELL + 1); }

static lv_obj_t *mk_cell(lv_color_t c) {
    lv_obj_t *o = lv_obj_create(s_field);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, CELL - 2, CELL - 2);
    lv_obj_set_style_bg_color(o, c, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(o, 3, LV_PART_MAIN);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

static bool on_snake(Pt p) {
    for (int i = 0; i < s_len; i++) {
        Pt b = s_body[(s_head - i + MAX_LEN) % MAX_LEN];
        if (b.x == p.x && b.y == p.y) return true;
    }
    return false;
}

static void drop_food() {
    do { s_fp = { (int8_t)random(GW), (int8_t)random(GH) }; } while (on_snake(s_fp));
    place(s_food, s_fp);
}

static void score_refresh() {
    char b[40]; snprintf(b, sizeof(b), "Score %d   Best %d", s_points, s_best);
    lv_label_set_text(s_score, b);
}

static void new_game() {
    for (int i = 0; i < MAX_LEN; i++) lv_obj_add_flag(s_seg[i], LV_OBJ_FLAG_HIDDEN);
    s_len = 3; s_head = 2; s_points = 0; s_dead = false;
    s_dx = s_ndx = 1; s_dy = s_ndy = 0;
    for (int i = 0; i < 3; i++) {
        s_body[i] = { (int8_t)(GW / 2 - 2 + i), (int8_t)(GH / 2) };
        place(s_seg[i], s_body[i]);
        lv_obj_clear_flag(s_seg[i], LV_OBJ_FLAG_HIDDEN);
    }
    drop_food();
    lv_obj_add_flag(s_msg, LV_OBJ_FLAG_HIDDEN);
    lv_timer_set_period(s_timer, STEP_MS0);
    score_refresh();
}

static void step(lv_timer_t *t) {
    if (s_dead) return;
    s_dx = s_ndx; s_dy = s_ndy;
    Pt h = s_body[s_head];
    Pt n = { (int8_t)(h.x + s_dx), (int8_t)(h.y + s_dy) };
    bool eat = (n.x == s_fp.x && n.y == s_fp.y);
    // The tail vacates its cell this step unless the snake grows.
    Pt tail = s_body[(s_head - s_len + 1 + MAX_LEN) % MAX_LEN];
    bool hit_self = on_snake(n) && !(!eat && n.x == tail.x && n.y == tail.y);
    if (n.x < 0 || n.y < 0 || n.x >= GW || n.y >= GH || hit_self) {
        s_dead = true;
        if (s_points > s_best) s_best = s_points;
        score_refresh();
        lv_label_set_text(s_msg, "Game over\nTap to play again");
        lv_obj_clear_flag(s_msg, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    // Every ring slot owns one square: show the new head's, hide the old tail's.
    s_head = (s_head + 1) % MAX_LEN;
    s_body[s_head] = n;
    place(s_seg[s_head], n);
    lv_obj_clear_flag(s_seg[s_head], LV_OBJ_FLAG_HIDDEN);
    if (eat && s_len < MAX_LEN) s_len++;
    else {
        int tail_i = (s_head - s_len + MAX_LEN) % MAX_LEN;   // slot that just fell off
        if (tail_i != s_head) lv_obj_add_flag(s_seg[tail_i], LV_OBJ_FLAG_HIDDEN);
    }
    if (eat) {
        s_points++;
        score_refresh();
        drop_food();
        int p = STEP_MS0 - s_points * 4;
        lv_timer_set_period(s_timer, p < STEP_MIN ? STEP_MIN : (uint32_t)p);
    }
}

static void field_cb(lv_event_t *e) {
    if (s_dead) { new_game(); return; }
    lv_point_t pt; lv_indev_get_point(lv_indev_get_act(), &pt);
    Pt h = s_body[s_head];
    int hx = h.x * CELL + CELL / 2, hy = h.y * CELL + CELL / 2 + UI_BAR_H;
    if (s_dx != 0) { s_ndx = 0; s_ndy = (pt.y < hy) ? -1 : 1; }
    else           { s_ndy = 0; s_ndx = (pt.x < hx) ? -1 : 1; }
}

static void exit_cb(lv_event_t *e) { if (s_root) lv_obj_del_async(s_root); }

static void root_deleted_cb(lv_event_t *e) {
    if (s_timer) { lv_timer_del(s_timer); s_timer = nullptr; }
    s_root = s_field = s_food = s_score = s_msg = nullptr;
}

void snake_open(lv_obj_t *parent) {
    if (s_root) return;
    s_root = lv_obj_create(parent);
    lv_obj_set_size(s_root, UI_W, UI_H);
    lv_obj_set_pos(s_root, 0, 0);
    lv_obj_set_style_bg_color(s_root, UI_COL_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_root, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_root, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_root, root_deleted_cb, LV_EVENT_DELETE, nullptr);

    lv_obj_t *bar = lv_obj_create(s_root);
    lv_obj_set_size(bar, UI_W, UI_BAR_H);
    lv_obj_set_pos(bar, 0, 0);
    ui_style_bar(bar);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
    s_score = lv_label_create(bar);
    lv_obj_set_style_text_font(s_score, UI_FONT_LABEL, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_score, UI_COL_GOOD, LV_PART_MAIN);
    lv_obj_align(s_score, LV_ALIGN_LEFT_MID, UI_SAFE, 0);
    lv_obj_t *x = lv_btn_create(bar);
    lv_obj_set_size(x, 90, UI_BAR_BTN_H);
    ui_hit_pad(x, UI_HIT_PAD);
    lv_obj_align(x, LV_ALIGN_RIGHT_MID, -UI_SAFE, 0);
    ui_style_outline_btn(x);
    lv_obj_set_style_pad_all(x, 0, LV_PART_MAIN);
    lv_obj_t *xl = lv_label_create(x);
    lv_label_set_text(xl, "Exit");
    lv_obj_set_style_text_color(xl, UI_COL_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(xl, UI_FONT_SMALL, LV_PART_MAIN);
    lv_obj_center(xl);
    lv_obj_add_event_cb(x, exit_cb, LV_EVENT_CLICKED, nullptr);

    s_field = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_field);
    lv_obj_set_size(s_field, GW * CELL, GH * CELL);
    lv_obj_set_pos(s_field, (UI_W - GW * CELL) / 2, UI_BAR_H + 1);
    lv_obj_set_style_bg_color(s_field, lv_color_hex(0x06060C), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_field, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_flag(s_field, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_field, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_field, field_cb, LV_EVENT_PRESSED, nullptr);

    s_food = mk_cell(UI_COL_WARN);
    for (int i = 0; i < MAX_LEN; i++) {
        s_seg[i] = mk_cell(lv_color_hex(0x5A9AE8));
        lv_obj_add_flag(s_seg[i], LV_OBJ_FLAG_HIDDEN);
    }
    s_len = 0;

    s_msg = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_msg, UI_FONT_VALUE, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_msg, UI_COL_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_align(s_msg, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_msg, lv_color_hex(0x0C1020), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_msg, LV_OPA_90, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_msg, UI_COL_ACCENT, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_msg, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(s_msg, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_msg, 14, LV_PART_MAIN);
    lv_obj_clear_flag(s_msg, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(s_msg);

    s_timer = lv_timer_create(step, STEP_MS0, nullptr);
    new_game();
}
