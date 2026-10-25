// SPDX-FileCopyrightText: 2026 DrEvan
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Debounced NVS save, 1 s after the last call. Defined in main.cpp.
void schedule_save();

// Re-drive every port's outputs from g_state (respects system_on, panel_on,
// port_active). Same logic as the boot restore. Defined in main.cpp.
void outputs_apply_all();
