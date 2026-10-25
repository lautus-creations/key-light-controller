// SPDX-FileCopyrightText: 2026 DrEvan
// SPDX-License-Identifier: GPL-3.0-or-later

/*
 * touch.cpp - FT6x36U I2C touch driver + LVGL input device
 *
 * If the controller stops answering (display module unplugged), polling stops
 * and a quiet address probe runs every TOUCH_REPROBE_MS instead. A failed
 * requestFrom() logs an [E] line in the core, so polling an absent controller
 * every LVGL tick would flood the serial monitor.
 */

#include "touch.h"
#include "board_pins.h"
#include <Wire.h>
#include "indicators.h"
#include "display.h"
#include <Arduino.h>

// The I2C bus is shared with the three panel ADS1115s and is brought up once
// by i2c_bus_init() (sense.cpp) at 100 kHz. Do not call Wire.begin() here.
#define CTP_ADDR  0x38
#define CTP_INT   PIN_CTP_INT
#define CTP_RST   PIN_CTP_RST

#define TOUCH_REPROBE_MS    500
#define TOUCH_FAILS_TO_DROP  10   // consecutive failed reads (each already retried once)

static bool     s_present    = false;
static uint8_t  s_fail_count = 0;
static uint32_t s_probe_ms   = 0;
static uint32_t s_err_count  = 0;   // failed reads while present (Diagnostics)

// Address-only write with STOP: quiet on NACK (no core error log).
static bool ft6x36_probe() {
    Wire.beginTransmission(CTP_ADDR);
    return Wire.endTransmission() == 0;
}

static void set_present(bool p) {
    if (p == s_present) return;
    s_present = p;
    s_fail_count = 0;
    ind_set_touch_missing(!p);
    Serial.println(p ? "[TOUCH] FT6x36U now answering at 0x38."
                     : "[TOUCH] FT6x36U stopped answering; polling paused, re-probing.");
}

// ── FT6x36 raw read ─────────────────────────────────────────
static bool ft6x36_read(int16_t *tx, int16_t *ty, uint8_t *pts) {
    Wire.beginTransmission(CTP_ADDR);
    Wire.write(0x02);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)CTP_ADDR, (uint8_t)6);
    if (Wire.available() < 6) { *pts = 0; while (Wire.available()) Wire.read(); return false; }
    *pts         =  Wire.read() & 0x0F;
    uint8_t xh   =  Wire.read();
    uint8_t xl   =  Wire.read();
    uint8_t yh   =  Wire.read();
    uint8_t yl   =  Wire.read();
    Wire.read();
    if (*pts == 0) { *tx = -1; *ty = -1; return true; }
    *tx = (int16_t)(((xh & 0x0F) << 8) | xl);
    *ty = (int16_t)(((yh & 0x0F) << 8) | yl);
    return true;
}

// ── LVGL input read callback ─────────────────────────────────
static void touch_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    int16_t tx = -1, ty = -1;
    uint8_t pts = 0;

    if (!s_present) {
        data->state = LV_INDEV_STATE_RELEASED;
        uint32_t now = millis();
        if (now - s_probe_ms >= TOUCH_REPROBE_MS) {
            s_probe_ms = now;
            if (ft6x36_probe()) set_present(true);
        }
        return;
    }

    // A read can be corrupted by noise on the display harness. Retry once; if
    // it still fails, repeat the last good state so a held finger is not
    // reported as lifted (which would turn a drag into taps).
    static bool    s_last_down = false;
    static int16_t s_last_x = 0, s_last_y = 0;
    bool ok = ft6x36_read(&tx, &ty, &pts);
    if (!ok) { s_err_count++; ok = ft6x36_read(&tx, &ty, &pts); }
    if (ok) {
        s_fail_count = 0;
    } else {
        s_err_count++;
        if (++s_fail_count >= TOUCH_FAILS_TO_DROP) {
            set_present(false);
            s_probe_ms = millis();
        }
        data->point.x = s_last_x;
        data->point.y = s_last_y;
        data->state   = s_last_down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
        return;
    }

    bool down = pts > 0 && tx >= 0 && ty >= 0;

    // Auto-dim: the first press on a dimmed screen only wakes it. Swallow that
    // press until the finger lifts so nothing underneath gets tapped.
    static bool s_swallow = false;
    static bool s_was_down = false;
    if (down && !s_was_down && display_touch_activity()) s_swallow = true;
    else if (down) display_touch_activity();   // keep the idle timer fresh while held
    s_was_down = down;
    if (s_swallow) {
        if (!down) s_swallow = false;
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    if (down) {
        // Controller reports portrait (x 0..319, y 0..479).
        // Landscape: x = ty, y = 319 - tx, clamped to the 480x320 screen.
        int16_t y = 319 - tx;
        if (y < 0) y = 0;
        data->point.x = ty > 479 ? 479 : ty;
        data->point.y = y;
        data->state   = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
    s_last_down = (data->state == LV_INDEV_STATE_PRESSED);
    s_last_x = data->point.x;
    s_last_y = data->point.y;
}

bool touch_init() {
    pinMode(CTP_RST, OUTPUT);
    digitalWrite(CTP_RST, LOW);  delay(15);
    digitalWrite(CTP_RST, HIGH); delay(300);   // FT6336U Trsi >= 300 ms (datasheet Table 3-5)

    s_present  = ft6x36_probe();
    s_probe_ms = millis();
    if (!s_present) {
        Serial.println("[TOUCH] WARN: FT6x36 not answering at 0x38 (display unplugged?). Re-probing.");
    } else {
        Serial.println("[TOUCH] FT6x36U found at 0x38.");
    }

    pinMode(CTP_INT, INPUT_PULLUP);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type    = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touch_read_cb;
    lv_indev_drv_register(&indev_drv);

    Serial.println("[TOUCH] LVGL input driver registered.");
    return s_present;
}
uint32_t touch_i2c_errors() { return s_err_count; }
