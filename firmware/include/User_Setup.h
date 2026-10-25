// SPDX-FileCopyrightText: 2026 DrEvan
// SPDX-License-Identifier: GPL-3.0-or-later

// NOT USED BY THE BUILD. TFT_eSPI is configured from platformio.ini
// build_flags (USER_SETUP_LOADED=1). Reference only; some values below differ
// from the build, which uses 27 MHz SPI, TFT_INVERSION_ON, TFT_RGB_ORDER
// TFT_BGR, 320x480.
//
//  Driver: ST7796U  (3.5" 320x480 SPI IPS module)
//  Interface: Hardware FSPI (GPIO10-13 are FSPI defaults on S3)
//
//  ┌─────────────┬──────────────────────────────────────────┐
//  │ Module pin  │ ESP32-S3 GPIO                            │
//  ├─────────────┼──────────────────────────────────────────┤
//  │ CS          │ GPIO10  (FSPI CS)                        │
//  │ DC / RS     │ GPIO9                                    │
//  │ RST         │ GPIO8                                    │
//  │ MOSI / SDI  │ GPIO11  (FSPI MOSI)                      │
//  │ SCK / CLK   │ GPIO12  (FSPI SCK)                       │
//  │ MISO / SDO  │ GPIO13  (FSPI MISO - optional, no wire)  │
//  │ LED / BL    │ GPIO14  (on/off, display.cpp)            │
//  │ VCC         │ 5V rail (module has onboard 3.3V reg)    │
//  └─────────────┴──────────────────────────────────────────┘
//
//  Before powering: the module's J1 solder jumper must NOT be bridged.
//  Bridging J1 bypasses the onboard regulator and puts VCC on the 3.3V IO
//  rail; with a 5V feed that damages the MCU.
//
//  Touch (FT6x36U, I2C) is handled in touch.cpp, not TFT_eSPI.

#pragma once

// ── Driver ──────────────────────────────────────────────────
#define ST7796_DRIVER

// ── SPI pins ────────────────────────────────────────────────
#define TFT_CS    10   // FSPI default CS
#define TFT_DC     9   // Data/Command
#define TFT_RST    8   // Reset
#define TFT_MOSI  11   // FSPI MOSI
#define TFT_SCLK  12   // FSPI SCK
#define TFT_MISO  13   // FSPI MISO (optional; wired but can leave NC)

// Backlight is driven by display.cpp, not TFT_eSPI.
// #define TFT_BL  14  // do NOT define here; causes conflict

// ── SPI speeds ──────────────────────────────────────────────
#define SPI_FREQUENCY       40000000
#define SPI_READ_FREQUENCY  20000000

// ── Fonts to compile in ─────────────────────────────────────
#define LOAD_GLCD    // Font 1 - built-in Adafruit 8pt
#define LOAD_FONT2   // Font 2 - small
#define LOAD_FONT4   // Font 4 - medium
#define LOAD_FONT6   // Font 6 - large numerals
#define LOAD_FONT7   // Font 7 - 7-segment style
#define LOAD_FONT8   // Font 8 - large
#define LOAD_GFXFF   // FreeFonts
#define SMOOTH_FONT  // Anti-aliased fonts (uses more flash)

// ── Color order ─────────────────────────────────────────────
// Most ST7796 modules ship BGR. If red appears blue, flip this.
// #define TFT_RGB_ORDER TFT_RGB   // uncomment if BGR not working
