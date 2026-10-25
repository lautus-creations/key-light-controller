// SPDX-FileCopyrightText: 2026 DrEvan
// SPDX-License-Identifier: GPL-3.0-or-later

/*
 * fault.cpp - Fault detection, latching and re-arm.
 * Compares sense readings against the commanded output state (pwm.h); trips
 * cut the panel via pwm_cut_panel(). Fault model is described in fault.h.
 */

#include "fault.h"
#include "sense.h"
#include "sense_config.h"
#include "pwm.h"
#include "app_state.h"
#include <Arduino.h>
#include <cstdio>
#include <cstring>

struct PanelFault {
    uint16_t flags;
    bool     latched;
    bool     held;               // re-arm: a request for light was blocked by the trip
    bool     off_seen;           // re-arm: a zero-output request followed
    uint8_t  cnt[16];            // per-bit debounce counters
};

static PanelFault s_pf[3];
static uint32_t   s_gen = 0;
static uint32_t   s_trip_count = 0;
static bool       s_no_supply = false;

static const char PANEL_CH[3] = { 'A', 'B', 'C' };

// Most recent fault event, for Diagnostics.
static bool     s_last_valid = false;
static uint8_t  s_last_panel = 0;
static uint16_t s_last_bit   = 0;
static uint32_t s_last_ms    = 0;

static void set_flags(uint8_t p, uint16_t f) {
    uint16_t old = s_pf[p].flags;
    if (old == f) return;
    uint16_t rise = f & ~old;
    if (rise) {
        uint16_t trips = rise & FT_TRIP_MASK;
        uint16_t pick  = trips ? trips : rise;           // trips outrank warnings
        s_last_bit   = (uint16_t)(pick & (uint16_t)(-(int16_t)pick));   // lowest set bit
        s_last_panel = p;
        s_last_ms    = millis();
        s_last_valid = true;
    }
    s_pf[p].flags = f;
    s_gen++;
}

static void trip(uint8_t p, uint16_t bit, const char *why) {
    PanelFault &pf = s_pf[p];
    bool first = !pf.latched;
    set_flags(p, pf.flags | bit);
    if (first) {
        // If light was demanded at the trip, the next Off counts toward re-arm.
        // Otherwise light must first be requested (and held), then Off -> On.
        pf.held     = pwm_demand(p);
        pf.off_seen = false;
        s_trip_count++;
    }
    pf.latched  = true;
    pwm_cut_panel(p);
    if (first) Serial.printf("[FAULT] Panel %c TRIPPED: %s — panel cut\n", PANEL_CH[p], why);
    else       Serial.printf("[FAULT] Panel %c also: %s\n", PANEL_CH[p], why);
}

// True only on the sweep where `cond` has held for `need` consecutive sweeps.
static bool debounce(uint8_t p, uint8_t bitpos, bool cond, uint8_t need) {
    uint8_t &c = s_pf[p].cnt[bitpos];
    if (!cond) { c = 0; return false; }
    if (c < 255) c++;
    return c == need;
}

// Set after `need` consecutive sweeps with cond, clear after `need` sweeps without.
static void warn(uint8_t p, uint16_t bit, uint8_t bitpos, bool cond, uint8_t need) {
    uint8_t &c = s_pf[p].cnt[bitpos];
    bool on = s_pf[p].flags & bit;
    if (cond != on) {
        if (++c >= need) { c = 0; set_flags(p, on ? (s_pf[p].flags & ~bit) : (s_pf[p].flags | bit)); }
    } else {
        c = 0;
    }
}

static inline uint8_t bitpos(uint16_t bit) { return (uint8_t)__builtin_ctz(bit); }

// ── Public ───────────────────────────────────────────────────
void fault_init() {
    memset(s_pf, 0, sizeof(s_pf));
    s_gen = 1;
    s_no_supply = false;
}

void fault_selftest() {
#if SELFTEST_ENABLE
    bool tested[3] = {false, false, false};
    SenseReading rd[3];
    bool ok[3] = {false, false, false};

    for (uint8_t p = 0; p < 3; p++) {
        if (!g_state.port_active[p] || !sense_present(p)) continue;
        tested[p] = true;
        pwm_selftest_enable(p, true);
    }
    if (!(tested[0] || tested[1] || tested[2])) {
        Serial.println("[SELFTEST] Skipped: no active port with a responding ADC.");
        return;
    }
    delay(SELFTEST_SETTLE_MS);

    int n_tested = 0, n_dead = 0;
    for (uint8_t p = 0; p < 3; p++) {
        if (!tested[p]) continue;
        ok[p] = sense_read_now(p, rd[p]);
        if (!ok[p]) { Serial.printf("[SELFTEST] Panel %c: ADC read failed\n", PANEL_CH[p]); continue; }
        n_tested++;
        if (rd[p].rail_v < RAIL_FUSE_OPEN_V) n_dead++;
        Serial.printf("[SELFTEST] Panel %c: rail=%.2fV  warm=%.3fA  cool=%.3fA (PWM 0)\n",
                      PANEL_CH[p], rd[p].rail_v, rd[p].amps[0], rd[p].amps[1]);
    }
    for (uint8_t p = 0; p < 3; p++) if (tested[p]) pwm_selftest_enable(p, false);

    // All tested rails dead (2+ panels) means no 24 V input, not multiple open
    // fuses: flag no-supply and latch nothing.
    if (n_tested >= 2 && n_dead == n_tested) {
        s_no_supply = true;
        s_gen++;
        Serial.println("[SELFTEST] All panel rails dead: 24 V input off or panel fuses not fitted. No trips latched.");
        return;
    }

    for (uint8_t p = 0; p < 3; p++) {
        if (!tested[p] || !ok[p]) continue;
        if (rd[p].rail_v < RAIL_FUSE_OPEN_V) {
            trip(p, FT_RAIL_DEAD, "self-test: rail dead with high-side on (fuse open / no 24 V)");
            continue;
        }
        for (uint8_t ch = 0; ch < 2; ch++) {
            float a = rd[p].amps[ch];        // raw: zero offset not yet applied
            if (a > STUCK_ON_A) {
                trip(p, ch == 0 ? FT_STUCK_WARM : FT_STUCK_COOL,
                     ch == 0 ? "self-test: warm current at PWM 0 (MOSFET stuck on)"
                             : "self-test: cool current at PWM 0 (MOSFET stuck on)");
            } else if (a <= ZERO_MAX_A) {
                sense_set_zero(p, ch, a);
            }
        }
    }
    // No light was requested yet, so re-arm requires a held request, then Off -> On.
    for (uint8_t p = 0; p < 3; p++) s_pf[p].held = false;
    Serial.println("[SELFTEST] Done.");
#endif
}

void fault_evaluate() {
    for (uint8_t p = 0; p < 3; p++) {
        if (!sense_online(p)) continue;
        const SenseReading &r = sense_get(p);
        if (r.sweep != sense_sweeps()) continue;      // ADC failed this sweep; skip stale data

        // Ages are measured from the start of the sweep, so a reading taken
        // before an output change is never judged against the new state.
        int32_t  hs_d     = (int32_t)(r.t_start_ms - pwm_hs_changed_ms(p));
        int32_t  duty_d   = (int32_t)(r.t_start_ms - pwm_duty_changed_ms(p));
        bool     hs       = pwm_hs_on(p);
        uint32_t hs_age   = hs_d   > 0 ? (uint32_t)hs_d   : 0;
        uint32_t duty_age = duty_d > 0 ? (uint32_t)duty_d : 0;
        bool rail_settled = hs && hs_age >= SETTLE_AFTER_ENABLE_MS;
        bool rail_dead    = rail_settled && r.rail_v <  RAIL_FUSE_OPEN_V;
        bool rail_low     = rail_settled && r.rail_v >= RAIL_FUSE_OPEN_V && r.rail_v < RAIL_LOW_V;

        if (rail_settled && r.rail_v >= RAIL_LOW_V && s_no_supply) { s_no_supply = false; s_gen++; }

        // ── Trips ────────────────────────────────────────────
        // With no 24 V supply (e.g. USB-only power) a dead rail is expected:
        // report no-supply instead of tripping.
        if (debounce(p, bitpos(FT_RAIL_DEAD), rail_dead && !s_no_supply, DEBOUNCE_SWEEPS))
            trip(p, FT_RAIL_DEAD, "rail dead with high-side on (fuse open / no 24 V)");

        bool hs_stuck = !hs && hs_age >= HS_DISCHARGE_HOLDOFF_MS && r.rail_v > RAIL_STUCK_HS_V;
        if (debounce(p, bitpos(FT_HS_STUCK), hs_stuck, DEBOUNCE_SWEEPS))
            trip(p, FT_HS_STUCK, "rail still energized with high-side off (P-FET stuck on)");

        bool i_valid = rail_settled && !rail_dead && duty_age >= SETTLE_AFTER_CHANGE_MS;
        bool open_c[2] = {false, false};
        float d_sum = pwm_duty_frac(p, 0) + pwm_duty_frac(p, 1);
        bool  no_current = i_valid && d_sum >= PANEL_OPEN_MIN_DUTY &&
                           (r.amps[0] + r.amps[1]) < 2.0f * OPEN_MAX_A;
        for (uint8_t ch = 0; ch < 2; ch++) {
            float d = pwm_duty_frac(p, ch);
            float lim = fault_oc_limit_a(p, ch);
            float a = r.amps[ch];
            bool stuck = i_valid && d == 0.0f && a > STUCK_ON_A;
            bool oc    = i_valid && d > 0.0f &&
                         (a > lim || (d >= OC_MIN_DUTY && a / d > lim));
            open_c[ch] = i_valid && d >= OPEN_MIN_DUTY && a < OPEN_MAX_A;

            uint16_t sb = ch == 0 ? FT_STUCK_WARM : FT_STUCK_COOL;
            uint16_t ob = ch == 0 ? FT_OC_WARM    : FT_OC_COOL;
            if (debounce(p, bitpos(sb), stuck, DEBOUNCE_SWEEPS))
                trip(p, sb, ch == 0 ? "warm current with PWM 0 (MOSFET stuck on)"
                                    : "cool current with PWM 0 (MOSFET stuck on)");
            if (debounce(p, bitpos(ob), oc, DEBOUNCE_SWEEPS_OC)) {
                char why[96];
                snprintf(why, sizeof(why), "%s over-current (%.2f A avg at %d%% duty, limit %.2f A)",
                         ch == 0 ? "warm" : "cool", a, (int)(d * 100 + 0.5f), lim);
                trip(p, ob, why);
            }
        }

        // ── Warnings ─────────────────────────────────────────
        warn(p, FW_NO_CURRENT, bitpos(FW_NO_CURRENT), no_current, DEBOUNCE_SWEEPS * 3);
        bool whole = no_current || (s_pf[p].flags & FW_NO_CURRENT);
        warn(p, FW_OPEN_WARM, bitpos(FW_OPEN_WARM), open_c[0] && !whole, DEBOUNCE_SWEEPS * 3);
        warn(p, FW_OPEN_COOL, bitpos(FW_OPEN_COOL), open_c[1] && !whole, DEBOUNCE_SWEEPS * 3);
        warn(p, FW_LOW_RAIL,  bitpos(FW_LOW_RAIL),  rail_low,  DEBOUNCE_SWEEPS * 3);
    }
}

void fault_tick() {
    static uint32_t last = 0;
    static uint32_t first = 0;
    uint32_t now = millis();
    if (!first) first = now;
    if (now - last < 250) return;
    last = now;
    if (now - first < 1000) return;   // allow the first sweeps to complete before judging offline
    for (uint8_t p = 0; p < 3; p++) {
        bool off = g_state.port_active[p] && !sense_online(p);
        uint16_t f = s_pf[p].flags;
        set_flags(p, off ? (f | FW_SENSE_OFFLINE) : (f & ~FW_SENSE_OFFLINE));
        // Open and low-rail warnings do not apply while the high-side is off.
        if (!pwm_hs_on(p)) set_flags(p, s_pf[p].flags & ~(FW_OPEN_WARM | FW_OPEN_COOL | FW_NO_CURRENT | FW_LOW_RAIL));
    }
}

bool fault_gate(uint8_t p, bool demand) {
    if (p > 2) return false;
    PanelFault &pf = s_pf[p];
    if (!pf.latched) return true;
    if (!demand) { if (pf.held) pf.off_seen = true; return true; }
    if (!pf.off_seen) { pf.held = true; return false; }

    // Re-arm: Off then On.
    pf.latched  = false;
    pf.held     = false;
    pf.off_seen = false;
    memset(pf.cnt, 0, sizeof(pf.cnt));
    set_flags(p, pf.flags & ~FT_TRIP_MASK);
    Serial.printf("[FAULT] Panel %c re-armed by user (Off -> On)\n", PANEL_CH[p]);
    return true;
}

float fault_oc_limit_a(uint8_t p, uint8_t ch) {
    if (p > 2 || ch > 1) return OC_DEFAULT_A;
    if (g_state.oc_ma[p]) return g_state.oc_ma[p] / 1000.0f;
    if (g_state.ref_ma[p][ch]) {
        float l = OC_REF_MULT * g_state.ref_ma[p][ch] / 1000.0f;
        if (l < OC_AUTO_MIN_A) l = OC_AUTO_MIN_A;
        if (l > OC_MAX_A)      l = OC_MAX_A;
        return l;
    }
    return OC_DEFAULT_A;
}

void fault_require_rearm(uint8_t p) {
    if (p > 2 || !s_pf[p].latched) return;
    s_pf[p].held     = false;   // same rule as a boot trip
    s_pf[p].off_seen = false;
}

void fault_reset(uint8_t p) {
    if (p > 2 || !s_pf[p].latched) return;
    PanelFault &pf = s_pf[p];
    pf.latched  = false;
    pf.held     = false;
    pf.off_seen = false;
    memset(pf.cnt, 0, sizeof(pf.cnt));
    set_flags(p, pf.flags & ~FT_TRIP_MASK);
    Serial.printf("[FAULT] Panel %c reset by user (Reset button)\n", PANEL_CH[p]);
}

uint16_t fault_flags(uint8_t p)   { return p < 3 ? s_pf[p].flags : 0; }
bool     fault_tripped(uint8_t p) { return p < 3 && s_pf[p].latched; }
bool     fault_no_supply()        { return s_no_supply; }
uint32_t fault_generation()       { return s_gen; }
uint32_t fault_trip_count()       { return s_trip_count; }

bool fault_any_trip() {
    for (uint8_t p = 0; p < 3; p++) if (s_pf[p].latched) return true;
    return false;
}

bool fault_any_warn() {
    if (s_no_supply) return true;
    for (uint8_t p = 0; p < 3; p++) if (s_pf[p].flags & FW_WARN_MASK) return true;
    return false;
}

int fault_group_level(int g) {
    if (g < 0 || g >= APP_MAX_GROUPS) return 0;
    int lvl = 0;
    for (int i = 0; i < g_state.groups[g].count; i++) {
        uint8_t p = g_state.groups[g].ports[i];
        if (p > 2) continue;
        if (s_pf[p].latched) return 2;
        if (s_pf[p].flags & FW_WARN_MASK) lvl = 1;
    }
    return lvl;
}

const char *fault_describe(uint8_t p, char *buf, size_t len) {
    buf[0] = '\0';
    if (p > 2) return buf;
    uint16_t f = s_pf[p].flags;
    struct { uint16_t bit; const char *txt; } map[] = {
        { FT_RAIL_DEAD,     "Panel rail dead: replace the panel fuse, or check the 24V input" },
        { FT_OC_WARM,       "Warm channel over-current: check panel wiring for a short" },
        { FT_OC_COOL,       "Cool channel over-current: check panel wiring for a short" },
        { FT_STUCK_WARM,    "Warm MOSFET stuck on (current with output off)" },
        { FT_STUCK_COOL,    "Cool MOSFET stuck on (current with output off)" },
        { FT_HS_STUCK,      "High-side switch stuck on (rail live with panel off)" },
        { FW_NO_CURRENT,    "Panel not drawing current: unplugged or wiring open" },
        { FW_OPEN_WARM,     "No warm current: panel unplugged or open string" },
        { FW_OPEN_COOL,     "No cool current: panel unplugged or open string" },
        { FW_LOW_RAIL,      "Panel rail voltage low" },
        { FW_SENSE_OFFLINE, "Current sensing offline (ADC not answering)" },
    };
    size_t off = 0;
    for (auto &m : map) {
        if (!(f & m.bit)) continue;
        int n = snprintf(buf + off, len - off, "%s%s", off ? "\n" : "", m.txt);
        if (n < 0 || (size_t)n >= len - off) break;
        off += n;
    }
    return buf;
}

void fault_clear_all() {
    for (uint8_t p = 0; p < 3; p++) {
        s_pf[p].latched  = false;
        s_pf[p].held     = false;
        s_pf[p].off_seen = false;
        memset(s_pf[p].cnt, 0, sizeof(s_pf[p].cnt));
        set_flags(p, s_pf[p].flags & ~FT_TRIP_MASK);
    }
    s_no_supply = false;
    s_gen++;
    Serial.println("[FAULT] All latches cleared from console.");
}

bool fault_last_event(uint8_t *panel, uint16_t *bit, uint32_t *ms) {
    if (!s_last_valid) return false;
    if (panel) *panel = s_last_panel;
    if (bit)   *bit   = s_last_bit;
    if (ms)    *ms    = s_last_ms;
    return true;
}

const char *fault_bit_name(uint16_t bit) {
    switch (bit) {
        case FT_OC_WARM:       return "Warm over-current";
        case FT_OC_COOL:       return "Cool over-current";
        case FT_STUCK_WARM:    return "Warm MOSFET stuck on";
        case FT_STUCK_COOL:    return "Cool MOSFET stuck on";
        case FT_RAIL_DEAD:     return "Rail dead (fuse / 24V)";
        case FT_HS_STUCK:      return "High-side stuck on";
        case FW_OPEN_WARM:     return "No warm current";
        case FW_OPEN_COOL:     return "No cool current";
        case FW_LOW_RAIL:      return "Low rail voltage";
        case FW_SENSE_OFFLINE: return "Sensing offline";
        case FW_NO_CURRENT:    return "Not drawing current";
        default:               return "Fault";
    }
}
