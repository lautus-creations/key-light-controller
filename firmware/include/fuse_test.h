// SPDX-FileCopyrightText: 2026 DrEvan
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <stdint.h>
#include "sense_config.h"

/*
 * fuse_test - turn-on surge cycling test for one panel's fuse (bring-up).
 *
 * Each cycle: turn the panel on exactly as the UI does (high-side + PWM
 * together, FUSE_TEST_BRI at FUSE_TEST_CCT) so the fuse sees the bulk-cap
 * charge surge plus LED load; hold FUSE_TEST_ON_MS; check rail is live and
 * current is flowing; then cut it fully (PWM 0 + high-side off) for
 * FUSE_TEST_OFF_MS. Stops on the first dead rail / no current and reports.
 *
 * Non-blocking: fuse_test_tick() from loop(). Owns the panel while running;
 * the UI page stops it on exit and restores normal outputs.
 */

enum FuseTestState : uint8_t { FT_IDLE, FT_ON, FT_OFF, FT_PASSED, FT_FAILED, FT_ABORTED };

struct FuseTestStatus {
    FuseTestState state;
    uint16_t cycle;         // current / last completed cycle (1-based)
    uint16_t total;
    float    last_rail_v;   // rail at end of the last ON period
    float    min_rail_v;    // lowest end-of-ON rail seen this run
    float    last_amps;     // warm + cool at end of the last ON period
};

void fuse_test_start();
void fuse_test_stop();      // abort; panel left off, caller restores outputs
void fuse_test_tick();      // call from loop()
bool fuse_test_running();
const FuseTestStatus &fuse_test_status();
