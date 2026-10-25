// SPDX-FileCopyrightText: 2026 DrEvan
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// NVS persistence: Arduino Preferences, namespace "klc" (keys in storage.cpp)
void storage_init();   // load defaults, then restore from NVS if present
void storage_save();   // write full g_state to NVS
void storage_load();   // read g_state from NVS
