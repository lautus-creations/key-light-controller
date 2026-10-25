// SPDX-FileCopyrightText: 2026 DrEvan
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
/*
 * klc_log.h - verbose serial logging switch
 *
 * Per-change logs ([PWM] slider moves, [OUT] high-side edges, [STORAGE]
 * saves) print only when verbose is on. Faults, boot, self-test and console
 * replies always print.
 *
 * Runtime toggle: console "log on" / "log off".
 * Boot default: KLC_VERBOSE_DEFAULT (-DKLC_VERBOSE_DEFAULT=1 to start verbose).
 */
#include <Arduino.h>

#ifndef KLC_VERBOSE_DEFAULT
#define KLC_VERBOSE_DEFAULT 0
#endif

extern bool g_klc_verbose;

#define VLOG(...) do { if (g_klc_verbose) Serial.printf(__VA_ARGS__); } while (0)
