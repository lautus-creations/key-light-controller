// SPDX-FileCopyrightText: 2026 DrEvan
// SPDX-License-Identifier: GPL-3.0-or-later

/*
 * sense.cpp - ADS1115 driver and non-blocking sweep.
 *
 * ADS1115 registers (TI SBAS444E 8.1):
 *   Pointer 0x00 = Conversion (16-bit two's complement)
 *   Pointer 0x01 = Config
 *   Config bits: [15] OS=1 start single-shot
 *                [14:12] MUX = 1xx -> AINx vs GND (100b=AIN0 .. 111b=AIN3)
 *                [11:9]  PGA = 001b -> ±4.096 V
 *                [8]     MODE = 1 single-shot
 *                [7:5]   DR (sense_config.h)
 *                [1:0]   COMP_QUE = 11b comparator off, ALERT/RDY hi-Z
 *
 * SBAS444E 7.5.1.1: the SDA-strapped address (0x4A, Panel C) requires SDA held
 * low >= 100 ns after SCL falls. Met at 100 kHz; suspect this first if only
 * Panel C's ADC is unreliable.
 */

#include "sense.h"
#include "sense_config.h"
#include "board_pins.h"
#include "fault.h"
#include <Arduino.h>
#include <Wire.h>
#include <string.h>

static const uint8_t ADS_ADDR[3] = { ADS_ADDR_A, ADS_ADDR_B, ADS_ADDR_C };

static bool         s_present[3];
static SenseReading s_rd[3];
static SenseReading s_work[3];       // sweep in progress
static bool         s_work_ok[3];
static float        s_zero[3][2];
static uint32_t     s_sweeps = 0;
static uint32_t     s_sweep_t0 = 0;

static uint8_t  s_ain      = 0;      // input being converted, 0..2
static bool     s_waiting  = false;
static uint32_t s_conv_t0  = 0;
static uint32_t s_reprobe_ms = 0;

static float    s_peak[3][2];        // highest channel current since boot / reset
static uint32_t s_i2c_err = 0;       // failed transactions to a present ADC

// ── Low level ────────────────────────────────────────────────
static uint16_t cfg_word(uint8_t ain) {
    return (uint16_t)(0x8000u
         | ((uint16_t)(0x4u | (ain & 0x3u)) << 12)
         | (0x1u << 9)                       // PGA ±4.096 V
         | (0x1u << 8)                       // single-shot
         | ((uint16_t)ADS_DR_BITS << 5)
         | 0x3u);                            // comparator disabled
}

static bool ads_start(uint8_t addr, uint8_t ain) {
    uint16_t c = cfg_word(ain);
    Wire.beginTransmission(addr);
    Wire.write(0x01);
    Wire.write((uint8_t)(c >> 8));
    Wire.write((uint8_t)(c & 0xFF));
    return Wire.endTransmission() == 0;
}

static bool ads_read(uint8_t addr, int16_t &out) {
    Wire.beginTransmission(addr);
    Wire.write(0x00);
    if (Wire.endTransmission() != 0) return false;
    if (Wire.requestFrom(addr, (uint8_t)2) != 2) return false;
    uint8_t hi = Wire.read();
    uint8_t lo = Wire.read();
    out = (int16_t)((hi << 8) | lo);
    return true;
}

static bool ads_probe(uint8_t addr) {
    Wire.beginTransmission(addr);
    return Wire.endTransmission() == 0;
}

static inline float counts_to_volts(int16_t raw) {
    if (raw < 0) raw = 0;                    // single-ended: negative = noise around 0
    return raw * (ADS_FSR_V / 32768.0f);
}

static void finalize(uint8_t p, SenseReading &r) {
    for (uint8_t ch = 0; ch < 2; ch++) {
        float a = counts_to_volts(r.raw[ch]) * AMPS_PER_VOLT - s_zero[p][ch];
        r.amps[ch] = a > 0 ? a : 0;
    }
    r.rail_v = counts_to_volts(r.raw[2]) * RAIL_DIV_RATIO;
    r.t_ms   = millis();
    r.valid  = true;
}

// ── Public ───────────────────────────────────────────────────
void i2c_bus_init() {
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_BUS_HZ);
    Wire.setTimeOut(20);
    Serial.printf("[I2C] Bus up: SDA=%d SCL=%d @ %lu Hz\n",
                  PIN_I2C_SDA, PIN_I2C_SCL, (unsigned long)I2C_BUS_HZ);
}

void sense_init() {
    for (uint8_t p = 0; p < 3; p++) {
        s_present[p] = ads_probe(ADS_ADDR[p]);
        s_rd[p]   = SenseReading{};
        s_zero[p][0] = s_zero[p][1] = 0;
        Serial.printf("[SENSE] Panel %c ADS1115 @0x%02X: %s\n", 'A' + p, ADS_ADDR[p],
                      s_present[p] ? "found" : "NOT FOUND");
    }
    s_ain = 0; s_waiting = false;
    s_reprobe_ms = millis();
}

bool sense_present(uint8_t p) { return p < 3 && s_present[p]; }

bool sense_online(uint8_t p) {
    if (p > 2 || !s_present[p] || !s_rd[p].valid) return false;
    return millis() - s_rd[p].t_ms < SENSE_STALE_MS;
}

const SenseReading &sense_get(uint8_t p) { return s_rd[p < 3 ? p : 0]; }
uint32_t sense_sweeps() { return s_sweeps; }

void  sense_set_zero(uint8_t p, uint8_t ch, float a) { if (p < 3 && ch < 2) s_zero[p][ch] = a; }
float sense_zero(uint8_t p, uint8_t ch)              { return (p < 3 && ch < 2) ? s_zero[p][ch] : 0; }

// Kick off conversion of input s_ain on every present ADC.
static void start_step() {
    bool any = false;
    for (uint8_t p = 0; p < 3; p++) {
        if (!s_present[p]) continue;
        if (s_ain == 0) { s_work[p] = SenseReading{}; s_work_ok[p] = true; }
        if (!ads_start(ADS_ADDR[p], s_ain)) { s_work_ok[p] = false; s_i2c_err++; }
        any = true;
    }
    if (!any) return;
    s_conv_t0 = millis();                 // conversion wait timed from the last start write
    if (s_ain == 0) s_sweep_t0 = s_conv_t0;
    s_waiting = true;
}

void sense_poll() {
    uint32_t now = millis();

    // Re-probe missing ADCs every 5 s (connector reseat, late power-up).
    if (now - s_reprobe_ms > 5000 && !s_waiting) {
        s_reprobe_ms = now;
        {
            for (uint8_t p = 0; p < 3; p++) {
                if (!s_present[p] && ads_probe(ADS_ADDR[p])) {
                    s_present[p] = true;
                    Serial.printf("[SENSE] Panel %c ADS1115 appeared\n", 'A' + p);
                }
            }
        }
    }

    if (!s_waiting) {
        start_step();
        return;
    }

    if (millis() - s_conv_t0 < ADS_CONV_WAIT_MS) return;

    for (uint8_t p = 0; p < 3; p++) {
        if (!s_present[p] || !s_work_ok[p]) continue;
        int16_t v;
        if (ads_read(ADS_ADDR[p], v)) s_work[p].raw[s_ain] = v;
        else                          { s_work_ok[p] = false; s_i2c_err++; }
    }
    s_waiting = false;

    if (++s_ain < 3) { start_step(); return; }   // next input right away
    s_ain = 0;

    s_sweeps++;
    for (uint8_t p = 0; p < 3; p++) {
        if (!s_present[p] || !s_work_ok[p]) continue;   // keep last good reading; its
        finalize(p, s_work[p]);                         // old sweep # marks it stale
        s_work[p].t_start_ms = s_sweep_t0;
        s_work[p].sweep      = s_sweeps;
        s_rd[p] = s_work[p];
        for (uint8_t ch = 0; ch < 2; ch++)
            if (s_rd[p].amps[ch] > s_peak[p][ch]) s_peak[p][ch] = s_rd[p].amps[ch];
    }
    fault_evaluate();
    start_step();
}

bool sense_read_now(uint8_t p, SenseReading &out) {
    if (p > 2 || !s_present[p]) return false;
    out = SenseReading{};
    for (uint8_t ain = 0; ain < 3; ain++) {
        if (!ads_start(ADS_ADDR[p], ain)) return false;
        delay(ADS_CONV_WAIT_MS);
        if (!ads_read(ADS_ADDR[p], out.raw[ain])) return false;
    }
    finalize(p, out);
    return true;
}

uint8_t sense_i2c_scan(uint8_t *found, uint8_t max) {
    uint8_t n = 0;
    for (uint8_t a = 0x08; a < 0x78 && n < max; a++) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() == 0) found[n++] = a;
    }
    return n;
}

float    sense_peak(uint8_t p, uint8_t ch) { return (p < 3 && ch < 2) ? s_peak[p][ch] : 0; }
void     sense_peak_reset()                { memset(s_peak, 0, sizeof(s_peak)); }
uint32_t sense_i2c_errors()                { return s_i2c_err; }
