// SPDX-FileCopyrightText: 2026 DrEvan
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <lvgl.h>

// Easter egg: Menu > About, press and hold the "Key Light Controller" title.
// Runs inside the Menu screen as a full-screen child; panels are untouched.
void snake_open(lv_obj_t *parent);
