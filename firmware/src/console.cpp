// SPDX-FileCopyrightText: 2026 DrEvan
// SPDX-License-Identifier: GPL-3.0-or-later

/*
 * console.cpp - serial diagnostics (commands listed in console.h)
 */

#include "console.h"
#include "sense.h"
#include "fault.h"
#include "pwm.h"
#include "app_state.h"
#include "save_manager.h"
#include "version.h"
#include "klc_log.h"
#include "panel_check.h"
#include "sysinfo.h"
#include "display.h"
#include "screens/screen_manager.h"
#include "screens/screen_main.h"
#include <Arduino.h>
#include <cstring>
#include <cstdlib>

bool g_klc_verbose = KLC_VERBOSE_DEFAULT;

static char     s_line[48];
static uint8_t  s_len = 0;
static uint32_t s_stream_ms = 0;
static uint32_t s_stream_last = 0;

static void print_sense() {
    Serial.printf("[SENSE] sweep #%lu\n", (unsigned long)sense_sweeps());
    Serial.println("  pnl  adc   rail_V   warm_A  duty_w   cool_A  duty_c  HS   zero_w  zero_c  flags");
    for (uint8_t p = 0; p < 3; p++) {
        const SenseReading &r = sense_get(p);
        bool on = sense_online(p);
        Serial.printf("  %c    %-4s  %6.2f   %6.3f  %5.1f%%   %6.3f  %5.1f%%  %-3s  %6.3f  %6.3f  0x%04X%s\n",
                      'A' + p, on ? "ok" : (sense_present(p) ? "stal" : "none"),
                      r.rail_v, r.amps[0], pwm_duty_frac(p, 0) * 100.0f,
                      r.amps[1], pwm_duty_frac(p, 1) * 100.0f,
                      pwm_hs_on(p) ? "on" : "off",
                      sense_zero(p, 0), sense_zero(p, 1),
                      fault_flags(p), fault_tripped(p) ? " TRIPPED" : "");
    }
}

static void print_faults() {
    char buf[384];
    if (fault_no_supply()) Serial.println("  System: no 24 V seen on panel rails at boot");
    for (uint8_t p = 0; p < 3; p++) {
        fault_describe(p, buf, sizeof(buf));
        Serial.printf("  Panel %c: %s flags=0x%04X\n    %s\n", 'A' + p,
                      fault_tripped(p) ? "TRIPPED" : "ok", fault_flags(p),
                      buf[0] ? buf : "(none)");
    }
}

// Same effect as the System toggle (panel < 0) or a panel's On/Off button:
// state change, outputs re-driven (fault gate still applies), saved.
static void set_output(int panel, bool on) {
    if (panel < 0) g_state.system_on = on;
    else           g_state.panel_on[panel] = on;
    outputs_apply_all();
    schedule_save();
    if (screen_manager_current() == SCR_MAIN) screen_main_refresh();
    if (panel < 0) Serial.printf("System %s\n", on ? "ON" : "OFF");
    else Serial.printf("%s %s%s\n", g_state.name[panel], on ? "ON" : "OFF",
                       (on && !g_state.system_on) ? " (System is OFF, so it stays dark)" : "");
}

static void handle(char *cmd) {
    while (*cmd == ' ') cmd++;
    size_t n = strlen(cmd);
    while (n && cmd[n - 1] == ' ') cmd[--n] = '\0';   // trailing spaces
    if (!*cmd) return;

    if (!strcmp(cmd, "help")) {
        Serial.println("Commands: sense | stream <ms> | stream off | faults | clear | i2c | ver\n"
                       "          check (panel check) | log on | log off\n"
                       "          off | on (whole system) | off A | on A (one panel, A/B/C)");
    } else if (!strcmp(cmd, "sense")) {
        print_sense();
    } else if (!strncmp(cmd, "stream", 6)) {
        const char *arg = cmd + 6;
        while (*arg == ' ') arg++;
        if (!strcmp(arg, "off") || !*arg) { s_stream_ms = 0; Serial.println("stream off"); }
        else {
            long ms = atol(arg);
            s_stream_ms = ms < 100 ? 100 : (uint32_t)ms;
            Serial.printf("stream every %lu ms (\"stream off\" to stop)\n", (unsigned long)s_stream_ms);
        }
    } else if (!strcmp(cmd, "faults")) {
        print_faults();
    } else if (!strcmp(cmd, "clear")) {
        fault_clear_all();
        Serial.println("Latches cleared. Panels stay dark until the next UI change.");
    } else if (!strcmp(cmd, "i2c")) {
        uint8_t found[16];
        uint8_t n = sense_i2c_scan(found, sizeof(found));
        Serial.printf("[I2C] %d device(s):", n);
        for (uint8_t i = 0; i < n; i++) Serial.printf(" 0x%02X", found[i]);
        Serial.println();
    } else if (!strcmp(cmd, "ver")) {
        char up[24];
        sysinfo_uptime_str(up, sizeof(up));
        Serial.printf("Key Light Controller v%s (%s)\n  uptime %s, last reset: %s\n  PWM %lu Hz, %s curve\n",
                      FW_VERSION, FW_BUILD, up, sysinfo_reset_reason(),
                      (unsigned long)pwm_freq_hz(), pwm_curve_perceptual() ? "perceptual" : "linear");
    } else if (!strcmp(cmd, "log on") || !strcmp(cmd, "log off")) {
        g_klc_verbose = !strcmp(cmd, "log on");
        Serial.printf("verbose logging %s\n", g_klc_verbose ? "ON" : "off");
    } else if (!strcmp(cmd, "off") || !strcmp(cmd, "on")) {
        set_output(-1, !strcmp(cmd, "on"));
    } else if ((!strncmp(cmd, "off ", 4) || !strncmp(cmd, "on ", 3))) {
        bool on = cmd[1] == 'n';
        char c = cmd[on ? 3 : 4];
        if (c >= 'a' && c <= 'c') c -= 32;
        if (c < 'A' || c > 'C' || cmd[on ? 4 : 5]) { Serial.println("usage: off A | on B | off | on"); return; }
        set_output(c - 'A', on);
    } else if (!strcmp(cmd, "check")) {
        if (panel_check_running()) Serial.println("Panel check already running");
        else panel_check_start();
    } else {
        Serial.printf("Unknown: \"%s\" (try help)\n", cmd);
    }
}

void console_tick() {
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\r') continue;
        if (c != '\n' && (c < 0x20 || c > 0x7E)) continue;   // drop control / non-ASCII bytes
        if (c == '\n') { s_line[s_len] = '\0'; handle(s_line); s_len = 0; continue; }
        if (s_len < sizeof(s_line) - 1) s_line[s_len++] = c;
    }
    if (s_stream_ms && millis() - s_stream_last >= s_stream_ms) {
        s_stream_last = millis();
        print_sense();
    }
}
