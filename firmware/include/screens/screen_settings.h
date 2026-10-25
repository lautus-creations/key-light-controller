// SPDX-FileCopyrightText: 2026 DrEvan
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <lvgl.h>

// Menu > Output: panel PWM frequency and dimming curve. Changes apply live
// and are saved to NVS.
lv_obj_t *settings_page_create(lv_obj_t *parent, lv_event_cb_t back_cb);
