// SPDX-FileCopyrightText: 2026 DrEvan
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <stdint.h>
#include <stdbool.h>

/*
 * panel_check.h - Guided per-channel light test (Diagnostics > Check).
 *
 * For each active port: warm alone at PANELCHECK_DUTY, then cool alone,
 * measuring average current and rail voltage.
 *
 * Each port has its own reference (g_state.ref_ma), learned from a known-good
 * panel with panel_check_save_reference(); no panel size is assumed.
 *   With a reference : PASS within ±PANELCHECK_REF_TOL, else LOW / HIGH.
 *   No reference     : NO REF (= current flows and the other channel is dark).
 *   Always           : NO CURRENT, WRONG CHANNEL (other channel lit), TRIPPED.
 * Outputs are restored from g_state when the check ends or is canceled.
 */

enum PcVerdict : uint8_t { PC_NOT_RUN = 0, PC_PASS, PC_LOW, PC_HIGH, PC_NONE, PC_XTALK, PC_SKIPPED, PC_TRIP, PC_NOREF };

struct PcChannel {
    PcVerdict verdict;
    float     amps;        // average current on the lit channel (A)
    float     other_amps;  // average current on the dark channel (A)
};

struct PcPanel {
    bool      tested;
    const char *skip_why;  // non-null if skipped
    float     rail_v;      // lowest step average (V)
    PcChannel ch[2];       // 0 = warm, 1 = cool
};

void  panel_check_start();
void  panel_check_cancel();
void  panel_check_tick();              // call from loop()
bool  panel_check_running();
bool  panel_check_done();              // finished (not canceled) since last start
int   panel_check_current_panel();     // -1 when idle
int   panel_check_current_ch();        // 0 warm / 1 cool / -1
const PcPanel &panel_check_result(uint8_t panel);
float panel_check_expected_a(uint8_t panel, uint8_t ch);   // at test duty; 0 = no reference

// After a completed check, store each channel's current scaled to 100 % duty
// as that port's reference. Skips channels with NO CURRENT / WRONG
// CHANNEL / TRIPPED. Returns a bitmask of ports saved; caller schedules the save.
uint8_t panel_check_save_reference();
bool    panel_check_can_save();
const char *panel_check_verdict_str(PcVerdict v);
