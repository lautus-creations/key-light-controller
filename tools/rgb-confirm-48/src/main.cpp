// SPDX-FileCopyrightText: 2026 DrEvan
// SPDX-License-Identifier: GPL-3.0-or-later

// GPIO 48 RGB LED confirmation — clone ESP32-S3 DevKit
// Cycles the onboard LED red -> green -> blue -> off on GPIO 48 only.
// If the LED follows the serial printout, GPIO 48 drives it.
// Run on the BARE spare board (GPIO 48 is Panel C cool PWM on the controller PCB).
#include <Arduino.h>

static const uint8_t PIN_RGB = 48;

static void show(const char *name, uint8_t r, uint8_t g, uint8_t b) {
    Serial.printf("GPIO %u -> %s\n", PIN_RGB, name);
    neopixelWrite(PIN_RGB, r, g, b);   // single pin only, so neopixelWrite is safe here
    delay(1000);
}

void setup() {
    Serial.begin(115200);
    delay(1500);
    Serial.println("\n=== GPIO 48 RGB confirmation ===");
}

void loop() {
    show("RED",   40,  0,  0);
    show("GREEN",  0, 40,  0);
    show("BLUE",   0,  0, 40);
    show("OFF",    0,  0,  0);
}
