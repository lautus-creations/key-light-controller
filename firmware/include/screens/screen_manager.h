// SPDX-FileCopyrightText: 2026 DrEvan
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

typedef enum {
    SCR_SPLASH = 0,
    SCR_MAIN,
    SCR_GROUPS,   // group configuration
    SCR_PANEL,
    SCR_MENU,
} screen_id_t;

void screen_manager_goto(screen_id_t id);
void screen_manager_goto_panel(int panel_idx);       // slide left  (forward)
void screen_manager_goto_panel_prev(int panel_idx);  // slide right (backward)
screen_id_t screen_manager_current();
void screen_manager_goto_diag(int return_panel);     // Menu > Diagnostics, Back -> panel detail
