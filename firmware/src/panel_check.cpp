// SPDX-FileCopyrightText: 2026 DrEvan
// SPDX-License-Identifier: GPL-3.0-or-later

/*
 * panel_check.cpp - Guided per-channel light test. See panel_check.h.
 */

#include "panel_check.h"
#include "sense_config.h"
#include "sense.h"
#include "fault.h"
#include "pwm.h"
#include "app_state.h"
#include "save_manager.h"
#include <Arduino.h>
#include <string.h>

enum Phase : uint8_t { PH_IDLE, PH_SETTLE, PH_MEASURE, PH_DONE };

static PcPanel  s_res[3];
static Phase    s_phase = PH_IDLE;
static int8_t   s_p  = -1;
static int8_t   s_ch = -1;
static uint32_t s_t0 = 0;
static uint32_t s_last_sweep = 0;
static float    s_sum[2], s_rail_sum;
static uint16_t s_n;
static bool     s_done = false;
static bool     s_tripped_here[3];   // tripped during this check; needs full re-arm

float panel_check_expected_a(uint8_t p, uint8_t ch) {
    if (p > 2 || ch > 1) return 0;
    return g_state.ref_ma[p][ch] / 1000.0f * PANELCHECK_DUTY;
}

static bool ch_saveable(const PcChannel &c) {
    return c.verdict == PC_PASS || c.verdict == PC_LOW || c.verdict == PC_HIGH || c.verdict == PC_NOREF;
}

bool panel_check_can_save() {
    if (!s_done) return false;
    for (uint8_t p = 0; p < 3; p++)
        if (s_res[p].tested && (ch_saveable(s_res[p].ch[0]) || ch_saveable(s_res[p].ch[1]))) return true;
    return false;
}

uint8_t panel_check_save_reference() {
    if (!panel_check_can_save()) return 0;
    uint8_t mask = 0;
    for (uint8_t p = 0; p < 3; p++) {
        if (!s_res[p].tested) continue;
        for (uint8_t ch = 0; ch < 2; ch++) {
            const PcChannel &c = s_res[p].ch[ch];
            if (!ch_saveable(c)) continue;
            float full_ma = c.amps / PANELCHECK_DUTY * 1000.0f;
            if (full_ma > PANELCHECK_REF_MAX_MA) continue;
            g_state.ref_ma[p][ch] = (uint16_t)(full_ma + 0.5f);
            mask |= 1u << p;
        }
        if (mask & (1u << p))
            Serial.printf("[CHECK] Panel %c reference saved: warm %u mA, cool %u mA (at 100%%)\n",
                          'A' + p, g_state.ref_ma[p][0], g_state.ref_ma[p][1]);
    }
    return mask;
}

static void light(uint8_t p, int ch) {
    pwm_set_channels_frac(p, ch == 0 ? PANELCHECK_DUTY : 0.0f, ch == 1 ? PANELCHECK_DUTY : 0.0f);
}

// Find the next (panel, channel) to test after the current one. False when finished.
static bool advance() {
    if (s_p >= 0 && s_ch == 0 && s_res[s_p].tested) { s_ch = 1; return true; }
    if (s_p >= 0) pwm_cut_panel((uint8_t)s_p);         // done with this panel
    while (++s_p <= 2) {
        PcPanel &r = s_res[s_p];
        if (!g_state.port_active[s_p]) { r.skip_why = "Port disabled"; continue; }
        if (fault_tripped(s_p))        { r.skip_why = "Tripped: reset first"; continue; }
        if (!sense_online(s_p))        { r.skip_why = "Sensing offline"; continue; }
        r.tested = true;
        s_ch = 0;
        return true;
    }
    return false;
}

static void begin_step() {
    light((uint8_t)s_p, s_ch);
    s_phase = PH_SETTLE;
    s_t0 = millis();
}

static void finish(bool completed) {
    if (s_p >= 0 && s_p <= 2) pwm_cut_panel((uint8_t)s_p);
    for (uint8_t p = 0; p < 3; p++) if (s_tripped_here[p]) fault_require_rearm(p);
    s_phase = completed ? PH_DONE : PH_IDLE;
    s_done  = completed;
    s_p = -1; s_ch = -1;
    outputs_apply_all();               // restore user output state
}

void panel_check_start() {
    if (panel_check_running()) return;
    memset(s_res, 0, sizeof(s_res));
    memset(s_tripped_here, 0, sizeof(s_tripped_here));
    s_done = false;
    s_p = -1; s_ch = -1;
    // All ports dark so only the channel under test draws current.
    for (uint8_t p = 0; p < 3; p++) pwm_cut_panel(p);
    Serial.printf("[CHECK] Panel check: each channel alone at %d%% duty\n", (int)(PANELCHECK_DUTY * 100));
    if (!advance()) { finish(true); return; }
    begin_step();
}

void panel_check_cancel() {
    if (!panel_check_running()) return;
    Serial.println("[CHECK] Canceled");
    finish(false);
}

static PcVerdict judge(uint8_t p, uint8_t ch, float a, float other) {
    float e = panel_check_expected_a(p, ch);
    if (a < OPEN_MAX_A)                       return PC_NONE;
    if (other > PANELCHECK_XTALK_A)           return PC_XTALK;
    if (e <= 0.0f)                            return PC_NOREF;
    if (a < e * (1.0f - PANELCHECK_REF_TOL))  return PC_LOW;
    if (a > e * (1.0f + PANELCHECK_REF_TOL))  return PC_HIGH;
    return PC_PASS;
}

void panel_check_tick() {
    if (!panel_check_running()) return;
    uint32_t el = millis() - s_t0;
    uint8_t p = (uint8_t)s_p;

    if (fault_tripped(p)) {                // tripped mid-test: record and move on
        s_res[p].ch[s_ch].verdict = PC_TRIP;
        s_tripped_here[p] = true;
        s_ch = 1;
        if (!advance()) { finish(true); return; }
        begin_step();
        return;
    }

    if (s_phase == PH_SETTLE) {
        if (el < PANELCHECK_SETTLE_MS) return;
        s_sum[0] = s_sum[1] = s_rail_sum = 0; s_n = 0;
        s_last_sweep = sense_sweeps();
        s_phase = PH_MEASURE;
        s_t0 = millis();
        return;
    }

    // PH_MEASURE: average every new sweep for this panel
    if (sense_sweeps() != s_last_sweep) {
        s_last_sweep = sense_sweeps();
        const SenseReading &r = sense_get(p);
        if (sense_online(p) && r.sweep == s_last_sweep) {
            s_sum[0] += r.amps[0]; s_sum[1] += r.amps[1]; s_rail_sum += r.rail_v; s_n++;
        }
    }
    if (el < PANELCHECK_MEASURE_MS) return;

    PcChannel &c = s_res[p].ch[s_ch];
    if (s_n == 0) {
        c.verdict = PC_SKIPPED;
        s_res[p].skip_why = "No sense data";
    } else {
        c.amps       = s_sum[s_ch] / s_n;
        c.other_amps = s_sum[1 - s_ch] / s_n;
        c.verdict    = judge(p, (uint8_t)s_ch, c.amps, c.other_amps);
        float rv = s_rail_sum / s_n;
        if (s_ch == 0 || rv < s_res[p].rail_v) s_res[p].rail_v = rv;
    }
    Serial.printf("[CHECK] Panel %c %s: %.3f A (ref %.3f, other %.3f A)  rail %.2f V  -> %s\n",
                  'A' + p, s_ch == 0 ? "warm" : "cool", c.amps, panel_check_expected_a(p, (uint8_t)s_ch),
                  c.other_amps, s_res[p].rail_v, panel_check_verdict_str(c.verdict));
    if (!advance()) { finish(true); return; }
    begin_step();
}

bool panel_check_running()       { return s_phase == PH_SETTLE || s_phase == PH_MEASURE; }
bool panel_check_done()          { return s_done; }
int  panel_check_current_panel() { return panel_check_running() ? s_p : -1; }
int  panel_check_current_ch()    { return panel_check_running() ? s_ch : -1; }
const PcPanel &panel_check_result(uint8_t p) { return s_res[p < 3 ? p : 0]; }

const char *panel_check_verdict_str(PcVerdict v) {
    switch (v) {
        case PC_PASS:    return "PASS";
        case PC_LOW:     return "LOW";
        case PC_HIGH:    return "HIGH";
        case PC_NONE:    return "NO CURRENT";
        case PC_XTALK:   return "WRONG CHANNEL";
        case PC_SKIPPED: return "SKIPPED";
        case PC_TRIP:    return "TRIPPED";
        case PC_NOREF:   return "NO REF";
        default:         return "--";
    }
}
