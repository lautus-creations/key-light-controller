// SPDX-FileCopyrightText: 2026 DrEvan
// SPDX-License-Identifier: GPL-3.0-or-later

/*
 * fuse_test.cpp - fuse turn-on cycling test.
 * Built only with ENABLE_FUSE_CYCLE_TEST (sense_config.h); stubs otherwise.
 */

#include "fuse_test.h"
#include "pwm.h"
#include "sense.h"
#include <Arduino.h>

#if ENABLE_FUSE_CYCLE_TEST

static FuseTestStatus s_st = { FT_IDLE, 0, FUSE_TEST_CYCLES, 0, 0, 0 };
static uint32_t       s_t0 = 0;
static const uint8_t  P    = FUSE_TEST_PANEL;

static void enter(FuseTestState st) { s_st.state = st; s_t0 = millis(); }

void fuse_test_start() {
    if (fuse_test_running()) return;
    pwm_cut_panel(P);                    // take the panel: PWM 0, rail off
    s_st = { FT_OFF, 0, FUSE_TEST_CYCLES, 0, 99.0f, 0 };
    s_t0 = millis() - FUSE_TEST_OFF_MS + 2000;   // first turn-on after 2 s settle
    Serial.printf("[FUSETEST] Panel %c: %u cycles, %u ms on / %u ms off\n",
                  'A' + P, FUSE_TEST_CYCLES, FUSE_TEST_ON_MS, FUSE_TEST_OFF_MS);
}

void fuse_test_stop() {
    if (!fuse_test_running()) return;
    pwm_cut_panel(P);
    enter(FT_ABORTED);
    Serial.printf("[FUSETEST] Aborted at cycle %u\n", s_st.cycle);
}

bool fuse_test_running() { return s_st.state == FT_ON || s_st.state == FT_OFF; }
const FuseTestStatus &fuse_test_status() { return s_st; }

void fuse_test_tick() {
    uint32_t el = millis() - s_t0;
    switch (s_st.state) {
    case FT_OFF:
        if (el < FUSE_TEST_OFF_MS) return;
        if (s_st.cycle >= s_st.total) {
            enter(FT_PASSED);
            Serial.printf("[FUSETEST] PASSED: %u turn-ons, fuse intact (min rail %.2f V)\n",
                          s_st.total, s_st.min_rail_v);
            return;
        }
        s_st.cycle++;
        pwm_set_panel(P, FUSE_TEST_BRI, FUSE_TEST_CCT);   // real turn-on: surge + LED load
        enter(FT_ON);
        return;

    case FT_ON: {
        if (el < FUSE_TEST_ON_MS) return;
        const SenseReading &r = sense_get(P);
        bool fresh = sense_online(P) && (int32_t)(r.t_start_ms - s_t0) > 0;
        float v = fresh ? r.rail_v : -1.0f;
        float a = fresh ? r.amps[0] + r.amps[1] : 0.0f;
        pwm_cut_panel(P);
        s_st.last_rail_v = v;
        s_st.last_amps   = a;
        if (v >= 0 && v < s_st.min_rail_v) s_st.min_rail_v = v;
        Serial.printf("[FUSETEST] cycle %u/%u  rail %.2f V  current %.3f A\n", s_st.cycle, s_st.total, v, a);
        if (!fresh) {                     // no sense data: can't judge, stop safely
            enter(FT_ABORTED);
            Serial.println("[FUSETEST] Aborted: no fresh rail reading (ADC offline?)");
            return;
        }
        if (v < FUSE_TEST_RAIL_OK_V || a < FUSE_TEST_MIN_A) {
            enter(FT_FAILED);
            Serial.printf("[FUSETEST] FAILED at cycle %u: rail %.2f V, current %.3f A (%s)\n", s_st.cycle, v, a,
                          v < FUSE_TEST_RAIL_OK_V ? "fuse open?" : "panel not drawing current: unplugged?");
            return;
        }
        enter(FT_OFF);
        return;
    }
    default:
        return;
    }
}

#else
void fuse_test_start() {}
void fuse_test_stop() {}
void fuse_test_tick() {}
bool fuse_test_running() { return false; }
const FuseTestStatus &fuse_test_status() { static FuseTestStatus s{}; return s; }
#endif
