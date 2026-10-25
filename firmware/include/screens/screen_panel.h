// SPDX-FileCopyrightText: 2026 DrEvan
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <lvgl.h>

// Per-panel detail screen. Call screen_manager_goto_panel(idx) to show.
lv_obj_t* screen_panel_create(int panel_idx);
