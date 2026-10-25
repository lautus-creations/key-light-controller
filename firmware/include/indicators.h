// SPDX-FileCopyrightText: 2026 DrEvan
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <stdint.h>

/*
 * indicators - MCU-LED1 (red, GPIO 2) and DevKit RGB (GPIO 38)
 *
 * Status LED (visible through the case):
 *   ON             booting (stuck lit = boot hung before the UI came up)
 *   OFF            all good
 *   1 Hz blink     warning (open channel, low rail, sense offline, no 24 V)
 *   ON             a panel is tripped
 *
 * DevKit RGB: failsafe when the display is dead or unplugged, dark in normal
 * operation so it never spills onto a set. Only driven when
 * BOARD_HAS_RGB_FAILSAFE is set (board_pins.h).
 *   dim white      booting
 *   off            all good
 *   amber, steady  warning
 *   blue, slow     no 24 V on panel rails (USB-only power)
 *   red, N blinks  panel N tripped (1 = A, 2 = B, 3 = C), repeating
 *   magenta        touch controller not found at boot
 */

void ind_init();                 // call early in setup(): status LED on, RGB dim white
void ind_boot_done();            // call at end of setup()
void ind_set_touch_missing(bool missing);
void ind_tick();                 // call from loop()
