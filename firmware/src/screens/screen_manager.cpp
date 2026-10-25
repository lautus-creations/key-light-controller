// SPDX-FileCopyrightText: 2026 DrEvan
// SPDX-License-Identifier: GPL-3.0-or-later

#include "klc_log.h"
#include "screens/screen_manager.h"
#include "screens/screen_splash.h"
#include "screens/screen_main.h"
#include "screens/screen_split.h"   // provides screen_groups_create()
#include "screens/screen_panel.h"
#include "screens/screen_menu.h"
#include <lvgl.h>
#include <Arduino.h>

static screen_id_t current = SCR_SPLASH;

void screen_manager_goto(screen_id_t id) {
    lv_obj_t *scr = nullptr;
    switch (id) {
        case SCR_SPLASH: scr = screen_splash_create(); break;
        case SCR_MAIN:   scr = screen_main_create();   break;
        case SCR_GROUPS: scr = screen_groups_create(); break;
        case SCR_MENU:   scr = screen_menu_create();   break;
        default: break;
    }
    if (scr) {
        lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
        current = id;
        VLOG("[SCR] -> %d\n", (int)id);
    }
}

void screen_manager_goto_panel(int panel_idx) {
    lv_obj_t *scr = screen_panel_create(panel_idx);
    if (scr) {
        lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, true);
        current = SCR_PANEL;
        VLOG("[SCR] -> PANEL %d\n", panel_idx);
    }
}

void screen_manager_goto_panel_prev(int panel_idx) {
    lv_obj_t *scr = screen_panel_create(panel_idx);
    if (scr) {
        lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 200, 0, true);
        current = SCR_PANEL;
        VLOG("[SCR] -> PANEL %d (prev)\n", panel_idx);
    }
}

void screen_manager_goto_diag(int return_panel) {
    lv_obj_t *scr = screen_menu_create_diag(return_panel);
    if (scr) {
        lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, true);
        current = SCR_MENU;
        VLOG("[SCR] -> DIAG (from panel %d)\n", return_panel);
    }
}

screen_id_t screen_manager_current() { return current; }
