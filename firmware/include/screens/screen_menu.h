// SPDX-FileCopyrightText: 2026 DrEvan
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <lvgl.h>
lv_obj_t* screen_menu_create();

// Menu screen opened straight onto Diagnostics (shortcut from panel detail).
// Back on Diagnostics returns to panel detail for return_panel.
lv_obj_t* screen_menu_create_diag(int return_panel);
