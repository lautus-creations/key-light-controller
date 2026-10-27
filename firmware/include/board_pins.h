// SPDX-FileCopyrightText: 2026 DrEvan
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
/*
 * board_pins.h - GPIO assignments, PCB rev 0.1 and 0.2 (same pinout), ESP32-S3-DevKitC-1.
 * Do not hardcode pin numbers anywhere else.
 */

#include <stdint.h>

// ── Display (TFT_eSPI owns MOSI/SCK/MISO/CS/DC via User_Setup.h) ──
#define PIN_LCD_RST        8
#define PIN_LCD_BL        14     // backlight, active HIGH

// ── Shared I2C bus: touch 0x38 + ADS1115 0x48/0x49/0x4A ──
#define PIN_I2C_SDA        4
#define PIN_I2C_SCL        5
#define PIN_CTP_INT       17
#define PIN_CTP_RST       18

// ── Per-channel low-side PWM (index = panel*2 + {0=warm,1=cool}) ──
#define PIN_PWM_A_WARM    39
#define PIN_PWM_A_COOL    40
#define PIN_PWM_B_WARM    41
#define PIN_PWM_B_COOL    42
#define PIN_PWM_C_WARM    47
#define PIN_PWM_C_COOL    48

// ── Per-panel high-side switch enables (NPN level shifter, active HIGH) ──
// Hardware default is OFF: 10k base pulldown on each NPN.
#define PIN_EN_A          15
#define PIN_EN_B          16
#define PIN_EN_C          21

// ── Indicators ──
#define PIN_STATUS_LED     2     // MCU-LED1 red, GPIO -> 1k -> LED -> GND (active HIGH)
#define PIN_RGB_LED       38     // DevKit v1.1 onboard WS2812

// 1 = genuine DevKitC-1 v1.1 (RGB LED on GPIO 38, usable as failsafe).
// 0 = clone DevKit with the RGB LED on GPIO 48: that pin carries Panel C cool
// PWM, so the LED latches random colors. Only a hardware change fixes it.
#define BOARD_HAS_RGB_FAILSAFE  0

static const uint8_t PWM_PINS[6] = {
    PIN_PWM_A_WARM, PIN_PWM_A_COOL,
    PIN_PWM_B_WARM, PIN_PWM_B_COOL,
    PIN_PWM_C_WARM, PIN_PWM_C_COOL,
};

static const uint8_t EN_PINS[3] = { PIN_EN_A, PIN_EN_B, PIN_EN_C };
