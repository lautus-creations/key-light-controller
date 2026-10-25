// SPDX-FileCopyrightText: 2026 DrEvan
// SPDX-License-Identifier: GPL-3.0-or-later

/*
 * sysinfo.cpp - reset reason, uptime formatting
 */

#include "sysinfo.h"
#include <Arduino.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <cstdio>

static esp_reset_reason_t s_rr = ESP_RST_UNKNOWN;

void sysinfo_init() { s_rr = esp_reset_reason(); }

const char *sysinfo_reset_reason() {
    switch (s_rr) {
        case ESP_RST_POWERON:   return "Power on";
        case ESP_RST_EXT:       return "Reset pin";
        case ESP_RST_SW:        return "Software restart";
        case ESP_RST_PANIC:     return "Crash (panic)";
        case ESP_RST_INT_WDT:   return "Watchdog (interrupt)";
        case ESP_RST_TASK_WDT:  return "Watchdog (UI loop hung)";
        case ESP_RST_WDT:       return "Watchdog";
        case ESP_RST_DEEPSLEEP: return "Wake from sleep";
        case ESP_RST_BROWNOUT:  return "Brown-out (supply dip)";
        case ESP_RST_SDIO:      return "SDIO";
        default:                return "Unknown";
    }
}

bool sysinfo_reset_abnormal() {
    return s_rr == ESP_RST_PANIC || s_rr == ESP_RST_INT_WDT || s_rr == ESP_RST_TASK_WDT ||
           s_rr == ESP_RST_WDT   || s_rr == ESP_RST_BROWNOUT;
}

void sysinfo_uptime_str(char *buf, unsigned len) {
    // millis() wraps after ~49.7 days; esp_timer is 64-bit microseconds.
    uint64_t s = (uint64_t)(esp_timer_get_time() / 1000000ULL);
    unsigned d = (unsigned)(s / 86400); s %= 86400;
    unsigned h = (unsigned)(s / 3600);  s %= 3600;
    unsigned m = (unsigned)(s / 60);    s %= 60;
    if (d) snprintf(buf, len, "%ud %02u:%02u:%02u", d, h, m, (unsigned)s);
    else   snprintf(buf, len, "%02u:%02u:%02u", h, m, (unsigned)s);
}

void sysinfo_ago_str(uint32_t then_ms, char *buf, unsigned len) {
    uint32_t a = (millis() - then_ms) / 1000;
    if (a < 60)          snprintf(buf, len, "%lu s ago",   (unsigned long)a);
    else if (a < 3600)   snprintf(buf, len, "%lu min ago", (unsigned long)(a / 60));
    else if (a < 86400)  snprintf(buf, len, "%lu h %lu min ago", (unsigned long)(a / 3600), (unsigned long)((a % 3600) / 60));
    else                 snprintf(buf, len, "%lu d ago",   (unsigned long)(a / 86400));
}
