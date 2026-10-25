// SPDX-FileCopyrightText: 2026 DrEvan
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <stdint.h>
#include <stdbool.h>

// Reset reason captured at boot (esp_reset_reason), for console + Diagnostics.
void        sysinfo_init();
const char *sysinfo_reset_reason();    // short text, e.g. "Power on", "Watchdog"
bool        sysinfo_reset_abnormal();  // watchdog, panic or brown-out
void        sysinfo_uptime_str(char *buf, unsigned len);   // "3d 04:12:09" / "04:12:09"
void        sysinfo_ago_str(uint32_t then_ms, char *buf, unsigned len);  // "12 s ago", "3 min ago"
