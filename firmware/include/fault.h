// SPDX-FileCopyrightText: 2026 DrEvan
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <stdint.h>
#include <stddef.h>

/*
 * fault.h - Per-panel fault detection, latching and re-arm.
 *
 * Trips (latched; panel cut: PWM 0 and high-side off):
 *   FT_OC_WARM / FT_OC_COOL       over-current on a channel
 *   FT_STUCK_WARM / FT_STUCK_COOL current flowing with that channel's PWM at 0
 *                                 (low-side MOSFET shorted)
 *   FT_RAIL_DEAD                  high-side enabled but panel rail ~0 V
 *                                 (fuse open, high-side failed, or no 24 V)
 *   FT_HS_STUCK                   high-side commanded off but rail still up
 *
 * Warnings (self-clearing; nothing is cut):
 *   FW_NO_CURRENT                 total duty >= PANEL_OPEN_MIN_DUTY but no current
 *                                 (unplugged / harness open), any CCT
 *   FW_OPEN_WARM / FW_OPEN_COOL   one channel at >= OPEN_MIN_DUTY draws nothing
 *                                 (one string open). Suppressed by FW_NO_CURRENT.
 *   FW_LOW_RAIL                   panel rail below RAIL_LOW_V while enabled
 *   FW_SENSE_OFFLINE              port is active but its ADC is not answering
 *
 * Only the tripped panel is cut; other panels keep running.
 *
 * Re-arm: Off then On. A zero-output request arms the reset only after a
 * request for light was held off by the trip (or the panel was lit when it
 * tripped); the next non-zero request clears the latch. Boot restore of a panel
 * saved as Off does not count. A persistent cause re-trips within ~250 ms.
 *
 * Reset (fault popup): clears the latch and the caller re-drives outputs from
 * saved state, so a panel set On lights immediately.
 *
 * Fault state is live only; nothing is logged to NVS.
 */

enum : uint16_t {
    FT_OC_WARM       = 1u << 0,
    FT_OC_COOL       = 1u << 1,
    FT_STUCK_WARM    = 1u << 2,
    FT_STUCK_COOL    = 1u << 3,
    FT_RAIL_DEAD     = 1u << 4,
    FT_HS_STUCK      = 1u << 5,
    FT_TRIP_MASK     = 0x00FF,

    FW_OPEN_WARM     = 1u << 8,
    FW_OPEN_COOL     = 1u << 9,
    FW_LOW_RAIL      = 1u << 10,
    FW_SENSE_OFFLINE = 1u << 11,
    FW_NO_CURRENT    = 1u << 12,
    FW_WARN_MASK     = 0xFF00,
};

void     fault_init();
void     fault_selftest();             // boot, after sense_init(), before panel restore
void     fault_evaluate();             // called by sense after each completed sweep
void     fault_tick();                 // call from loop() (offline checks)

// Clear one panel's trip latch (Reset button). Caller re-drives outputs.
void     fault_reset(uint8_t panel);

// Output gate, called by pwm_set_panel(). Returns true if output may flow.
bool     fault_gate(uint8_t panel, bool demand);

uint16_t fault_flags(uint8_t panel);
bool     fault_tripped(uint8_t panel);
bool     fault_any_trip();
bool     fault_any_warn();
bool     fault_no_supply();            // self-test saw every tested rail dead (no 24 V)
uint32_t fault_generation();           // increments on any flag change (UI polling)
uint32_t fault_trip_count();           // increments on every new trip (incl. re-trips)

// 0 = ok, 1 = warning, 2 = tripped; worst across the ports of group g
int      fault_group_level(int g);

// Human-readable summary of one panel's flags. Returns buf.
const char *fault_describe(uint8_t panel, char *buf, size_t len);

void     fault_clear_all();            // console: drop latches. Outputs are not re-driven;
                                       // a panel lights again on the next UI request.

// Force a full re-arm (light requested and held, then Off -> On) for a trip
// that occurred while Panel Check, not the user, drove the panel, so the
// restore that follows cannot clear it.
void        fault_require_rearm(uint8_t panel);

// Over-current limit in amps; see sense_config.h for the selection rule.
float       fault_oc_limit_a(uint8_t panel, uint8_t ch);

// Most recent fault event (trip or warning rising). False if none since boot.
bool        fault_last_event(uint8_t *panel, uint16_t *bit, uint32_t *ms);
const char *fault_bit_name(uint16_t bit);   // short label for one flag bit
