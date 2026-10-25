// SPDX-FileCopyrightText: 2026 DrEvan
// SPDX-License-Identifier: GPL-3.0-or-later

// RGB LED pin finder — clone ESP32-S3 DevKit
// ---------------------------------------------------------------------------
// The factory firmware on these boards colour-cycles the onboard RGB LED, but
// neither GPIO 38 nor GPIO 48 drives it with addressable (WS2812) data. This
// sketch tries every GPIO that is safe to drive on an N16R8 module, two ways:
//
//   1. WS2812 data (green, 0.8 s)      -> lights if the LED is addressable on this pin
//   2. Plain HIGH 0.4 s, then LOW 0.4 s -> lights if it's a simple RGB LED
//      (one colour per pin; common-anode types light on LOW)
//
// Watch the LED and the serial monitor. When the LED reacts, note the GPIO and
// which phase ("ws2812", "HIGH" or "LOW") was printed just before it.
//
// Skipped (not safe / not available on N16R8): 0, 3, 45, 46 (strapping),
// 19, 20 (USB), 26-32 (flash), 33-37 (octal PSRAM).
// RUN ON THE BARE SPARE BOARD ONLY — never seated in the controller PCB.
//
// NOTE: does NOT use neopixelWrite(). In arduino-esp32 2.0.x that function
// binds its RMT driver to the FIRST pin it is ever called with and silently
// keeps sending to that pin afterwards (esp32-hal-rgb-led.c: static
// "initialized"). That bug made tools/devkit-rev-test drive GPIO 38 twice and
// never test GPIO 48. Here each pin gets its own RMT init/deinit.
// ---------------------------------------------------------------------------
#include <Arduino.h>

// One WS2812 frame on an arbitrary pin (GRB order, 800 kHz timing @ 100 ns tick).
static void ws2812(uint8_t pin, uint8_t r, uint8_t g, uint8_t b) {
    rmt_obj_t *rmt = rmtInit(pin, RMT_TX_MODE, RMT_MEM_64);
    if (!rmt) { Serial.printf("  (RMT init failed on GPIO %u)\n", pin); return; }
    rmtSetTick(rmt, 100);
    rmt_data_t d[24];
    int col[3] = { g, r, b };
    int i = 0;
    for (int c = 0; c < 3; c++)
        for (int bit = 7; bit >= 0; bit--, i++) {
            bool one = col[c] & (1 << bit);
            d[i].level0 = 1; d[i].duration0 = one ? 8 : 4;
            d[i].level1 = 0; d[i].duration1 = one ? 4 : 8;
        }
    rmtWriteBlocking(rmt, d, 24);
    delayMicroseconds(100);          // >50 us low = latch
    rmtDeinit(rmt);
}

static const uint8_t PINS[] = {
    1, 2, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 21,
    38, 39, 40, 41, 42, 43, 44, 47, 48
};

void setup() {
    Serial.begin(115200);
    delay(1500);
    Serial.println("\n=== RGB LED pin finder ===");
    Serial.println("Watch the onboard RGB LED. Note the GPIO + phase when it reacts.");
    for (uint8_t p : PINS) { pinMode(p, INPUT); }
}

void loop() {
    for (uint8_t p : PINS) {
        Serial.printf("GPIO %2u  ws2812 green\n", p);
        ws2812(p, 0, 40, 0);
        delay(800);
        ws2812(p, 0, 0, 0);
        delay(100);

        pinMode(p, OUTPUT);
        Serial.printf("GPIO %2u  HIGH\n", p);
        digitalWrite(p, HIGH);
        delay(400);
        Serial.printf("GPIO %2u  LOW\n", p);
        digitalWrite(p, LOW);
        delay(400);
        pinMode(p, INPUT);
        delay(100);
    }
    Serial.println("--- pass complete, repeating ---");
}
