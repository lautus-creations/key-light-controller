// SPDX-FileCopyrightText: 2026 DrEvan
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <cstdio>     // snprintf
#include <cstring>    // strlen

// ── Constants ────────────────────────────────────────────────
#define APP_MAX_PORTS   3
#define APP_MAX_GROUPS  3
#define APP_NAME_LEN    20    // max custom panel name length (incl. null)
#define APP_PRESET_CNT  3     // Tungsten / Neutral / Daylight

// ── Group ────────────────────────────────────────────────────
// A group is a set of ports that share the same Bri/CCT controls.
struct Group {
    uint8_t ports[APP_MAX_PORTS];   // port indices (0=A, 1=B, 2=C)
    uint8_t count;                  // number of ports in this group (0 = unused)
};

// ── AppState ─────────────────────────────────────────────────
struct AppState {
    // ── Hardware config (set in Settings) ─
    bool    port_active[APP_MAX_PORTS];          // is port A/B/C physically connected?
    char    name[APP_MAX_PORTS][APP_NAME_LEN];   // custom name per port

    // ── Per-port live state ─
    uint8_t  brightness[APP_MAX_PORTS];          // 0-100 %
    uint16_t cct[APP_MAX_PORTS];                 // 2700-6500 K
    bool     panel_on[APP_MAX_PORTS];            // per-panel on/off

    // ── System ─
    bool     system_on;                          // master on/off

    // ── Groups ─
    Group    groups[APP_MAX_GROUPS];
    uint8_t  group_count;                        // how many groups are active

    // ── Presets ─
    struct {
        char     name[12];
        uint8_t  brightness;
        uint16_t cct;
    } preset[APP_PRESET_CNT];

    // ── Display & output settings (Menu > Display, Menu > Panel Tools) ─
    uint8_t  bl_level;                           // screen backlight 10-100 % (stored, not applied)
    uint8_t  bl_dim_min;                         // auto-dim minutes, 0 = never (stored, not applied)
    uint8_t  dim_curve;                          // 0 = linear, 1 = perceptual (default)
    uint8_t  pwm_khz;                            // panel PWM frequency in kHz (20/25/30/39)

    // ── Panel Check reference (per port, per channel; 0 = none saved) ─
    // Current the channel draws at 100 % duty, in mA, learned from a check on a
    // known-good panel. Lets mixed panel sizes share one controller.
    uint16_t ref_ma[APP_MAX_PORTS][2];
    // Over-current trip override per port, mA (0 = automatic from ref_ma).
    uint16_t oc_ma[APP_MAX_PORTS];
};

extern AppState g_state;

// Initialise g_state to factory defaults
void app_state_init();

// ── Group helpers (inline) ────────────────────────────────────

// First active port index in a group, or 0 if empty
static inline uint8_t group_first_port(int g) {
    if (g_state.groups[g].count == 0) return 0;
    return g_state.groups[g].ports[0];
}

static inline uint8_t  group_brightness(int g) {
    return g_state.brightness[group_first_port(g)];
}
static inline uint16_t group_cct(int g) {
    return g_state.cct[group_first_port(g)];
}
static inline bool group_on(int g) {
    for (int i = 0; i < g_state.groups[g].count; i++)
        if (g_state.panel_on[g_state.groups[g].ports[i]]) return true;
    return false;
}
static inline const char* group_name(int g) {
    if (g_state.groups[g].count == 0) return "-";
    return g_state.name[group_first_port(g)];
}
static inline void group_set_brightness(int g, uint8_t bri) {
    for (int i = 0; i < g_state.groups[g].count; i++)
        g_state.brightness[g_state.groups[g].ports[i]] = bri;
}
static inline void group_set_cct(int g, uint16_t cct) {
    for (int i = 0; i < g_state.groups[g].count; i++)
        g_state.cct[g_state.groups[g].ports[i]] = cct;
}
static inline void group_set_on(int g, bool on) {
    for (int i = 0; i < g_state.groups[g].count; i++)
        g_state.panel_on[g_state.groups[g].ports[i]] = on;
}

// Rebuild groups so every active port is its own group (all separate)
static inline void groups_separate_all() {
    g_state.group_count = 0;
    for (int p = 0; p < APP_MAX_PORTS; p++) {
        if (!g_state.port_active[p]) continue;
        int g = g_state.group_count++;
        g_state.groups[g].ports[0] = (uint8_t)p;
        g_state.groups[g].count    = 1;
    }
}

// Rebuild groups so all active ports form a single linked group
static inline void groups_link_all() {
    g_state.groups[0].count = 0;
    for (int p = 0; p < APP_MAX_PORTS; p++) {
        if (!g_state.port_active[p]) continue;
        g_state.groups[0].ports[g_state.groups[0].count++] = (uint8_t)p;
    }
    g_state.group_count = (g_state.groups[0].count > 0) ? 1 : 0;
}

// Build a display name string for group g: all port names joined with " + ".
// Writes at most `len` bytes (including null terminator) into `buf`.
static inline void group_name_str(int g, char *buf, size_t len) {
    buf[0] = '\0';
    size_t off = 0;
    for (int pi = 0; pi < g_state.groups[g].count; pi++) {
        int pp = g_state.groups[g].ports[pi];
        if (pi > 0 && off + 4 < len) {
            snprintf(buf + off, len - off, " + ");
            off = strlen(buf);
        }
        snprintf(buf + off, len - off, "%s", g_state.name[pp]);
        off = strlen(buf);
    }
}

// Defensive check on group topology loaded from NVS.
// If group_count is out of range, or any group has zero ports, reset to
// all-separate so screens never encounter an empty group.
// Call from storage_load() after loading group data.
static inline void groups_validate() {
    bool bad = (g_state.group_count == 0 || g_state.group_count > APP_MAX_GROUPS);
    if (!bad) {
        for (int g = 0; g < g_state.group_count; g++) {
            if (g_state.groups[g].count == 0) { bad = true; break; }
        }
    }
    if (bad) groups_separate_all();
}
