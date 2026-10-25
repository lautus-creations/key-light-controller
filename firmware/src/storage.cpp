// SPDX-FileCopyrightText: 2026 DrEvan
// SPDX-License-Identifier: GPL-3.0-or-later

/*
 * storage.cpp - NVS persistence
 *
 * Namespace "klc". Do not rename: existing saved data would be lost.
 * X = port, group or preset index 0-2. "gcnt" is the has-data sentinel.
 *
 *   sys_on          bool     system master on/off
 *   pXact           bool     port active
 *   pXbri           uint8    brightness, %
 *   pXcct           uint16   CCT, K
 *   pXon            bool     panel on/off
 *   pXnm            string   panel name
 *   gcnt            uint8    group count
 *   gXcnt           uint8    port count in group X
 *   gXpY            uint8    port index, group X slot Y
 *   prXbri          uint8    preset brightness
 *   prXcct          uint16   preset CCT
 *   prXnm           string   preset name
 *   bl_lvl          uint8    backlight level (stored, not applied)
 *   bl_dim          uint8    auto-dim minutes (stored, not applied)
 *   curve           uint8    0 linear, 1 perceptual
 *   pwm_khz         uint8    panel PWM frequency, kHz
 *   pXrefw/pXrefc   uint16   Panel Check reference, mA at 100 % (0 = none)
 *   pXoc            uint16   over-current trip override, mA (0 = auto)
 *
 * Keys missing from older saves load with their defaults.
 */

#include "storage.h"
#include "app_state.h"
#include <Preferences.h>
#include <Arduino.h>
#include <string.h>
#include "klc_log.h"
#include "sense_config.h"   // PANELCHECK_REF_MAX_MA

AppState g_state;
static Preferences prefs;

// ── Factory defaults ──────────────────────────────────────────
void app_state_init() {
    // All 3 ports active by default (user can disable in Settings)
    for (int p = 0; p < APP_MAX_PORTS; p++) {
        g_state.port_active[p] = true;
        g_state.brightness[p]  = 75;
        g_state.cct[p]         = 3200;
        g_state.panel_on[p]    = true;
    }
    // Default panel names
    strncpy(g_state.name[0], "Panel A", APP_NAME_LEN);
    strncpy(g_state.name[1], "Panel B", APP_NAME_LEN);
    strncpy(g_state.name[2], "Panel C", APP_NAME_LEN);

    // System on
    g_state.system_on = true;

    // Default grouping: all separate (one group per port)
    groups_separate_all();

    // Presets: Tungsten / Neutral / Daylight
    strncpy(g_state.preset[0].name, "Tungsten",  12);
    g_state.preset[0].brightness = 75;
    g_state.preset[0].cct        = 3200;

    strncpy(g_state.preset[1].name, "Neutral",   12);
    g_state.preset[1].brightness = 75;
    g_state.preset[1].cct        = 4500;

    strncpy(g_state.preset[2].name, "Daylight",  12);
    g_state.preset[2].brightness = 75;
    g_state.preset[2].cct        = 5600;

    g_state.bl_level   = 100;
    g_state.bl_dim_min = 0;
    g_state.dim_curve  = 1;      // perceptual
    g_state.pwm_khz    = 25;
    memset(g_state.ref_ma, 0, sizeof(g_state.ref_ma));   // no panel assumed
    memset(g_state.oc_ma, 0, sizeof(g_state.oc_ma));     // trip limit automatic
}

static bool pwm_khz_valid(uint8_t k) { return k == 20 || k == 25 || k == 30 || k == 39; }

static void settings_sanitize() {
    if (g_state.bl_level < 10)  g_state.bl_level = 10;
    if (g_state.bl_level > 100) g_state.bl_level = 100;
    if (g_state.bl_dim_min > 60) g_state.bl_dim_min = 0;
    if (g_state.dim_curve > 1)  g_state.dim_curve = 1;
    if (!pwm_khz_valid(g_state.pwm_khz)) g_state.pwm_khz = 25;
    for (int p = 0; p < APP_MAX_PORTS; p++)
        for (int c = 0; c < 2; c++)
            if (g_state.ref_ma[p][c] > PANELCHECK_REF_MAX_MA) g_state.ref_ma[p][c] = 0;
    for (int p = 0; p < APP_MAX_PORTS; p++)
        if (g_state.oc_ma[p] && (g_state.oc_ma[p] < OC_MANUAL_MIN_MA || g_state.oc_ma[p] > OC_MANUAL_MAX_MA))
            g_state.oc_ma[p] = 0;
}

// ── Public API ────────────────────────────────────────────────
void storage_init() {
    app_state_init();   // load defaults first

    prefs.begin("klc", true);
    bool has_data = prefs.isKey("gcnt");   // new-format sentinel
    prefs.end();

    if (!has_data) {
        Serial.println("[STORAGE] No saved data — using defaults.");
        return;
    }
    storage_load();
}

void storage_save() {
    prefs.begin("klc", false);
    char key[12];

    prefs.putBool("sys_on", g_state.system_on);

    for (int p = 0; p < APP_MAX_PORTS; p++) {
        snprintf(key, sizeof(key), "p%dact", p);
        prefs.putBool(key, g_state.port_active[p]);
        snprintf(key, sizeof(key), "p%dbri", p);
        prefs.putUChar(key, g_state.brightness[p]);
        snprintf(key, sizeof(key), "p%dcct", p);
        prefs.putUShort(key, g_state.cct[p]);
        snprintf(key, sizeof(key), "p%don", p);
        prefs.putBool(key, g_state.panel_on[p]);
        snprintf(key, sizeof(key), "p%dnm", p);
        prefs.putString(key, g_state.name[p]);
    }

    prefs.putUChar("gcnt", g_state.group_count);
    for (int g = 0; g < g_state.group_count; g++) {
        snprintf(key, sizeof(key), "g%dcnt", g);
        prefs.putUChar(key, g_state.groups[g].count);
        for (int s = 0; s < g_state.groups[g].count; s++) {
            snprintf(key, sizeof(key), "g%dp%d", g, s);
            prefs.putUChar(key, g_state.groups[g].ports[s]);
        }
    }

    for (int i = 0; i < APP_PRESET_CNT; i++) {
        snprintf(key, sizeof(key), "pr%dbri", i);
        prefs.putUChar(key, g_state.preset[i].brightness);
        snprintf(key, sizeof(key), "pr%dcct", i);
        prefs.putUShort(key, g_state.preset[i].cct);
        snprintf(key, sizeof(key), "pr%dnm", i);
        prefs.putString(key, g_state.preset[i].name);
    }

    prefs.putUChar("bl_lvl",  g_state.bl_level);
    prefs.putUChar("bl_dim",  g_state.bl_dim_min);
    prefs.putUChar("curve",   g_state.dim_curve);
    prefs.putUChar("pwm_khz", g_state.pwm_khz);
    for (int p = 0; p < APP_MAX_PORTS; p++) {
        snprintf(key, sizeof(key), "p%drefw", p); prefs.putUShort(key, g_state.ref_ma[p][0]);
        snprintf(key, sizeof(key), "p%drefc", p); prefs.putUShort(key, g_state.ref_ma[p][1]);
        snprintf(key, sizeof(key), "p%doc", p);   prefs.putUShort(key, g_state.oc_ma[p]);
    }

    prefs.end();
    VLOG("[STORAGE] Saved.\n");
}

void storage_load() {
    prefs.begin("klc", true);
    char key[12];

    g_state.system_on = prefs.getBool("sys_on", true);

    for (int p = 0; p < APP_MAX_PORTS; p++) {
        snprintf(key, sizeof(key), "p%dact", p);
        g_state.port_active[p] = prefs.getBool(key, true);
        snprintf(key, sizeof(key), "p%dbri", p);
        g_state.brightness[p] = prefs.getUChar(key, 75);
        snprintf(key, sizeof(key), "p%dcct", p);
        g_state.cct[p] = prefs.getUShort(key, 3200);
        snprintf(key, sizeof(key), "p%don", p);
        g_state.panel_on[p] = prefs.getBool(key, true);
        snprintf(key, sizeof(key), "p%dnm", p);
        String nm = prefs.getString(key, g_state.name[p]);
        strncpy(g_state.name[p], nm.c_str(), APP_NAME_LEN - 1);
        g_state.name[p][APP_NAME_LEN - 1] = '\0';
    }

    // Group topology; groups_validate() repairs bad data. Presets load regardless.
    g_state.group_count = prefs.getUChar("gcnt", 0);
    if (g_state.group_count > 0 && g_state.group_count <= APP_MAX_GROUPS) {
        for (int g = 0; g < g_state.group_count; g++) {
            snprintf(key, sizeof(key), "g%dcnt", g);
            g_state.groups[g].count = prefs.getUChar(key, 0);
            for (int s = 0; s < g_state.groups[g].count && s < APP_MAX_PORTS; s++) {
                snprintf(key, sizeof(key), "g%dp%d", g, s);
                g_state.groups[g].ports[s] = prefs.getUChar(key, (uint8_t)s);
            }
        }
    }
    groups_validate();   // resets to all-separate if topology is corrupt/empty

    for (int i = 0; i < APP_PRESET_CNT; i++) {
        snprintf(key, sizeof(key), "pr%dbri", i);
        g_state.preset[i].brightness = prefs.getUChar(key, g_state.preset[i].brightness);
        snprintf(key, sizeof(key), "pr%dcct", i);
        g_state.preset[i].cct = prefs.getUShort(key, g_state.preset[i].cct);
        snprintf(key, sizeof(key), "pr%dnm", i);
        String pnm = prefs.getString(key, g_state.preset[i].name);
        strncpy(g_state.preset[i].name, pnm.c_str(), sizeof(g_state.preset[i].name) - 1);
        g_state.preset[i].name[sizeof(g_state.preset[i].name) - 1] = '\0';
    }

    g_state.bl_level   = prefs.getUChar("bl_lvl",  g_state.bl_level);
    g_state.bl_dim_min = prefs.getUChar("bl_dim",  g_state.bl_dim_min);
    g_state.dim_curve  = prefs.getUChar("curve",   g_state.dim_curve);
    g_state.pwm_khz    = prefs.getUChar("pwm_khz", g_state.pwm_khz);
    for (int p = 0; p < APP_MAX_PORTS; p++) {
        snprintf(key, sizeof(key), "p%drefw", p); g_state.ref_ma[p][0] = prefs.getUShort(key, 0);
        snprintf(key, sizeof(key), "p%drefc", p); g_state.ref_ma[p][1] = prefs.getUShort(key, 0);
        snprintf(key, sizeof(key), "p%doc", p);   g_state.oc_ma[p]     = prefs.getUShort(key, 0);
    }
    settings_sanitize();

    prefs.end();
    Serial.println("[STORAGE] Loaded.");
}
