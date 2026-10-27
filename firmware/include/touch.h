// SPDX-FileCopyrightText: 2026 DrEvan
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <lvgl.h>

// Initialize FT6x36U and register LVGL input driver.
// Call after display_init() in setup(). The shared I2C bus must already be up
// (i2c_bus_init()). Returns false if the controller did not ACK at 0x38.
bool touch_init();
uint32_t touch_i2c_errors();   // failed reads while the controller was present
