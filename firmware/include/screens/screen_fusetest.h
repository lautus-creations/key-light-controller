// SPDX-FileCopyrightText: 2026 DrEvan
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <lvgl.h>
// Menu > Fuse Cycle Test: bench tool, built only with ENABLE_FUSE_CYCLE_TEST.
lv_obj_t *fusetest_page_create(lv_obj_t *parent, lv_event_cb_t back_cb);
