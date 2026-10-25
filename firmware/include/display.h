// SPDX-FileCopyrightText: 2026 DrEvan
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <lvgl.h>
#include <stdint.h>

void display_init();

// ── Backlight ───────────────────────────────────────────────
void    display_set_level(uint8_t pct);        // user level 10-100 %, applied now
uint8_t display_level();
void    display_set_autodim(uint8_t minutes);  // 0 = never dim
void    display_fade_in(uint32_t ms);          // ramp from current to user level
void    display_tick();                        // call from loop(): fades + auto-dim
bool    display_is_dimmed();
// Touch driver calls this on every press. Returns true if the screen was
// dimmed: the press woke it and must not reach the UI.
bool    display_touch_activity();
void    display_wake();
void    display_set_brightness(uint8_t val);   // 0 = off, >0 = user level
