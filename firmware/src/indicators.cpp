// SPDX-FileCopyrightText: 2026 DrEvan
// SPDX-License-Identifier: GPL-3.0-or-later

/*
 * indicators.cpp - status LED + DevKit RGB patterns
 *
 * RGB uses neopixelWrite() (arduino-esp32 2.0.x; rgbLedWrite() on 3.x).
 * Writes only on change to keep RMT idle.
 */

#include "indicators.h"
#include "board_pins.h"
#include "fault.h"
#include <Arduino.h>

#define RGB_LVL 24   // keep it dim

static bool     s_booting = true;
static bool     s_touch_missing = false;
static uint32_t s_rgb_last = 0xFFFFFFFF;
static bool     s_led_last = false;

static void rgb(uint8_t r, uint8_t g, uint8_t b) {
#if !BOARD_HAS_RGB_FAILSAFE
    (void)r; (void)g; (void)b;
    return;
#endif
    uint32_t v = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
    if (v == s_rgb_last) return;
    neopixelWrite(PIN_RGB_LED, r, g, b);
    s_rgb_last = v;
}

static void led(bool on) {
    if (on == s_led_last) return;
    digitalWrite(PIN_STATUS_LED, on ? HIGH : LOW);
    s_led_last = on;
}

void ind_init() {
    pinMode(PIN_STATUS_LED, OUTPUT);
    digitalWrite(PIN_STATUS_LED, HIGH);
    s_led_last = true;
    rgb(RGB_LVL / 2, RGB_LVL / 2, RGB_LVL / 2);
}

void ind_boot_done()                 { s_booting = false; }
void ind_set_touch_missing(bool m)   { s_touch_missing = m; }

void ind_tick() {
    if (s_booting) return;
    uint32_t now = millis();

    // ── Status LED ───────────────────────────────────────────
    if (fault_any_trip())      led(true);
    else if (fault_any_warn()) led((now / 500) & 1);
    else                       led(false);

    // ── RGB ──────────────────────────────────────────────────
    int tripped = -1;
    for (uint8_t p = 0; p < 3; p++) if (fault_tripped(p)) { tripped = p; break; }

    if (tripped >= 0) {
        // N blinks (250 ms on / 250 ms off), then 1.5 s dark, repeating
        uint32_t n      = (uint32_t)tripped + 1;
        uint32_t period = n * 500 + 1500;
        uint32_t t      = now % period;
        bool on = (t < n * 500) && ((t % 500) < 250);
        on ? rgb(RGB_LVL * 2, 0, 0) : rgb(0, 0, 0);
    } else if (s_touch_missing) {
        rgb(RGB_LVL, 0, RGB_LVL);
    } else if (fault_no_supply()) {
        ((now / 1000) & 1) ? rgb(0, 0, RGB_LVL) : rgb(0, 0, 0);
    } else if (fault_any_warn()) {
        rgb(RGB_LVL, RGB_LVL / 2, 0);
    } else {
        rgb(0, 0, 0);
    }
}
