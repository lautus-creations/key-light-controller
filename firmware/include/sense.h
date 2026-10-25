// SPDX-FileCopyrightText: 2026 DrEvan
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <stdint.h>

/*
 * sense.h - Panel current and rail sensing, 3x ADS1115 at 0x48/0x49/0x4A.
 *
 * Per panel: AIN0 = warm INA181, AIN1 = cool INA181, AIN2 = +24V_PANEL_x
 * divider, AIN3 unused.
 *
 * One sweep converts AIN0..2 on all present ADCs in parallel (~20 ms, ~50 Hz).
 * fault_evaluate() runs after each completed sweep.
 */

struct SenseReading {
    float    amps[2];      // warm, cool: average current (A), zero-corrected, >= 0
    float    rail_v;       // +24V_PANEL_x (V)
    int16_t  raw[3];       // last raw ADS counts (AIN0..2)
    uint32_t t_ms;         // millis() when this sweep completed
    uint32_t t_start_ms;   // millis() when this sweep's first conversion started
    uint32_t sweep;        // sense_sweeps() value this reading belongs to
    bool     valid;        // at least one complete sweep since boot
};

void  i2c_bus_init();                       // Wire on SDA 4 / SCL 5 at I2C_BUS_HZ
void  sense_init();                         // probe ADCs; call after i2c_bus_init()
void  sense_poll();                         // call every loop()
bool  sense_present(uint8_t panel);         // ADC ACKed at init
bool  sense_online(uint8_t panel);          // present AND fresh data
const SenseReading &sense_get(uint8_t panel);
uint32_t sense_sweeps();                    // completed sweep counter

// Blocking single sweep for one panel (boot self-test only). Returns false on I2C error.
bool  sense_read_now(uint8_t panel, SenseReading &out);

// Zero offset (amps) subtracted from each channel. Set by the boot self-test.
void  sense_set_zero(uint8_t panel, uint8_t ch, float amps);
float sense_zero(uint8_t panel, uint8_t ch);

// Bus scan for the serial console
uint8_t sense_i2c_scan(uint8_t *found, uint8_t max);

// ── Diagnostics ─────────────────────────────────────────────
float    sense_peak(uint8_t panel, uint8_t ch);   // highest reading since boot / reset (A)
void     sense_peak_reset();
uint32_t sense_i2c_errors();                      // failed reads/writes to a present ADC
