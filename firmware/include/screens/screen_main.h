// SPDX-FileCopyrightText: 2026 DrEvan
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <lvgl.h>
lv_obj_t* screen_main_create();
void screen_main_refresh();  // redraw values after an external state change
