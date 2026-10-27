// SPDX-FileCopyrightText: 2026 DrEvan
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

/*
 * fault_ui.h - On-screen fault reporting.
 *
 *  - Trip modal on LVGL's top layer (any screen). OK dismisses the message;
 *    the trip stays latched until Reset or Off -> On.
 *  - One-time notice if the boot self-test found no 24 V on the panel rails.
 *  - Main-screen status dot color: red = tripped, amber = warning.
 *
 * Call fault_ui_init() after display_init() and screen_manager_goto(SCR_SPLASH).
 */

#include <lvgl.h>

void       fault_ui_init();

// Clears every tripped panel's latch and re-drives outputs from saved state.
// Used by the popup and Diagnostics Reset buttons.
void       fault_ui_reset_all();
lv_color_t fault_dot_color(int group, bool on);
