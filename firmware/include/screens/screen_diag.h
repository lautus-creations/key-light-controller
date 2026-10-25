// SPDX-FileCopyrightText: 2026 DrEvan
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <lvgl.h>

/*
 * screen_diag.h - Diagnostics sub-page (Menu > Diagnostics).
 *
 * One row per panel: rail voltage, warm/cool current and duty, high-side
 * state, status line. Footer: ADC status, no-24 V notice, and Check, System
 * and Reset buttons. Reset is the same action as the fault popup's Reset.
 *
 * Refreshes every 250 ms from a page-owned lv_timer deleted with the page.
 * Only Reset and Panel Check drive outputs.
 */
lv_obj_t *diag_page_create(lv_obj_t *parent, lv_event_cb_t back_cb);
