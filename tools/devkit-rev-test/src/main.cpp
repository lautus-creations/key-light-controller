// SPDX-FileCopyrightText: 2026 DrEvan
// SPDX-License-Identifier: GPL-3.0-or-later

// ESP32-S3-DevKitC-1 board-revision test
// ---------------------------------------------------------------------------
// The onboard addressable RGB LED moved pins between DevKit revisions:
//     v1.1  ->  GPIO 38      (GPIO 48 is a free GPIO -> OK for PC-cool PWM)
//     v1.0  ->  GPIO 48      (GPIO 48 drives the LED -> conflicts with our PWM)
//
// This sketch drives each candidate pin in turn with a distinct color. Watch
// the single onboard RGB LED (next to the BOOT/RST buttons):
//
//     Blinks RED  only  ->  GPIO 38 controls it  ->  board is v1.1  (48 FREE)
//     Blinks BLUE only  ->  GPIO 48 controls it  ->  board is v1.0  (48 = LED)
//     Never lights      ->  neither pin is the RGB (check board / core version)
//
// RUN THIS ON THE BARE DEVKIT over USB. If you run it with the DevKit seated
// in the PCB, the GPIO-48 phase will briefly pulse the PC-cool MOSFET gate and
// you'll see that panel flicker -- harmless, but cleaner to test standalone.
// ---------------------------------------------------------------------------

#include <Arduino.h>

// FIX (Oct 20 2026): neopixelWrite() in arduino-esp32 2.0.x binds its RMT
// driver to the FIRST pin it is ever called with and keeps sending there, so
// the original version of this sketch drove GPIO 38 twice and never tested
// GPIO 48. Each write now gets its own RMT init/deinit on the requested pin.
static void ledWrite(uint8_t pin, uint8_t r, uint8_t g, uint8_t b) {
  rmt_obj_t *rmt = rmtInit(pin, RMT_TX_MODE, RMT_MEM_64);
  if (!rmt) return;
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
  delayMicroseconds(100);
  rmtDeinit(rmt);
}
#define neopixelWrite ledWrite

static const int PIN_V11 = 38;   // onboard RGB LED pin on v1.1
static const int PIN_V10 = 48;   // onboard RGB LED pin on v1.0

static void allOff() {
  neopixelWrite(PIN_V11, 0, 0, 0);
  neopixelWrite(PIN_V10, 0, 0, 0);
}

void setup() {
  Serial.begin(115200);
  delay(1500);                   // allow native USB-CDC to enumerate
  Serial.println();
  Serial.println("=== ESP32-S3-DevKitC-1 revision test ===");
  Serial.println("Watch the onboard RGB LED (by the BOOT/RST buttons):");
  Serial.println("  RED  blink  -> GPIO 38 -> board is v1.1  (GPIO 48 FREE for PC-cool PWM)");
  Serial.println("  BLUE blink  -> GPIO 48 -> board is v1.0  (GPIO 48 = LED -> CONFLICT)");
  Serial.println("  never lights-> neither pin drives the RGB (check board/core)");
  Serial.println();
}

void loop() {
  allOff();
  Serial.println("driving GPIO 38 (v1.1 pin) -> RED");
  neopixelWrite(PIN_V11, 40, 0, 0);
  delay(1200);

  allOff();
  Serial.println("driving GPIO 48 (v1.0 pin) -> BLUE");
  neopixelWrite(PIN_V10, 0, 0, 40);
  delay(1200);

  allOff();
  delay(400);
}
