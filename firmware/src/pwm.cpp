// SPDX-FileCopyrightText: 2026 DrEvan
// SPDX-License-Identifier: GPL-3.0-or-later

/*
 * pwm.cpp - panel output driver (high-side enable + low-side PWM)
 *
 * LEDC channel map (10-bit, 20/25/30/39 kHz, default 25):
 *   ch0 = GPIO39  Panel A Warm      ch1 = GPIO40  Panel A Cool
 *   ch2 = GPIO41  Panel B Warm      ch3 = GPIO42  Panel B Cool
 *   ch4 = GPIO47  Panel C Warm      ch5 = GPIO48  Panel C Cool
 * Each pair shares one timer (0, 1, 2). LEDC runs from the 40 MHz XTAL and IDF
 * needs a divider of at least 256 (1.0), so 10-bit tops out at
 * 40e6 / 1024 = 39.06 kHz.
 *
 * High-side enables: GPIO 15/16/21, active HIGH into the NPN level shifter.
 * 10k base pulldowns hold them off until firmware drives them.
 *
 * Brightness -> light output:
 *   level = curve(brightness_pct), 0.0 .. 1.0 of full output
 *     linear     : pct / 100
 *     perceptual : CIE 1976 L* inverse (lightness -> luminance) so each 1 %
 *                  step looks even, plus PERC_FLOOR so 1 % is visibly lit.
 * CCT mix:   warm_ratio = (6500 - cct_k) / 3800
 *            cool_ratio = (cct_k - 2700) / 3800
 *            counts     = round(level * ratio * PWM_MAX)
 * Any channel carrying at least half the mix gets at least PWM_MIN_COUNTS
 * while brightness > 0, so a panel is never "on" but dark.
 */

#include "pwm.h"
#include "board_pins.h"
#include "sense_config.h"
#include "fault.h"
#include "klc_log.h"
#include <Arduino.h>
#include <math.h>

#define PWM_BITS        10
#define PWM_MAX         ((1u << PWM_BITS) - 1)   // 1023 (ledcWrite turns this into full-on)
#define PWM_DEFAULT_HZ  25000
#define PWM_MIN_COUNTS  2          // ~80 ns at 25 kHz
#define PERC_FLOOR      0.003f     // perceptual curve: 1 % -> ~0.4 % output

struct PanelOut {
    uint16_t duty[2];          // as written to LEDC (0..PWM_MAX)
    bool     demand;           // caller asked for non-zero output (before gating)
    bool     hs_on;
    uint32_t hs_changed_ms;
    uint32_t duty_changed_ms;
    uint32_t zero_since_ms;    // when duty last became 0/0 (for deferred HS off)
    uint8_t  last_bri;         // for log de-duplication
    uint16_t last_cct;
};

static PanelOut s_out[3];
static bool     s_perceptual = true;
static uint32_t s_freq_hz    = PWM_DEFAULT_HZ;

static inline uint8_t ledc_ch(uint8_t panel, uint8_t ch) { return panel * 2 + ch; }

static void hs_write(uint8_t p, bool on) {
    if (s_out[p].hs_on == on) return;
    digitalWrite(EN_PINS[p], on ? HIGH : LOW);
    s_out[p].hs_on         = on;
    s_out[p].hs_changed_ms = millis();
    VLOG("[OUT] Panel %c high-side %s\n", 'A' + p, on ? "ON" : "OFF");
}

static void duty_write(uint8_t p, uint16_t w, uint16_t c) {
    PanelOut &o = s_out[p];
    if (o.duty[0] == w && o.duty[1] == c) return;
    bool was_zero = (o.duty[0] == 0 && o.duty[1] == 0);
    ledcWrite(ledc_ch(p, PWM_CH_WARM), w);
    ledcWrite(ledc_ch(p, PWM_CH_COOL), c);
    o.duty[0] = w;
    o.duty[1] = c;
    o.duty_changed_ms = millis();
    if (w == 0 && c == 0 && !was_zero) o.zero_since_ms = o.duty_changed_ms;
}

// Gate, sequence and write one panel. Returns true if output was held off by a trip.
static bool apply(uint8_t p, uint16_t w, uint16_t c) {
    PanelOut &o = s_out[p];
    o.demand = (w | c) != 0;
    bool held = false;
    // Fault gate: a latched trip holds this panel dark until re-armed.
    if (!fault_gate(p, o.demand)) { w = 0; c = 0; held = o.demand; }
    if (w | c) hs_write(p, true);   // rail up first (µs-scale switch)
    duty_write(p, w, c);
    // High-side turn-off on zero demand is deferred to pwm_tick().
    return held;
}

// ── Pre-init (first line of setup) ───────────────────────────
void pwm_preinit_safe() {
    for (uint8_t i = 0; i < 6; i++) { pinMode(PWM_PINS[i], OUTPUT); digitalWrite(PWM_PINS[i], LOW); }
    for (uint8_t p = 0; p < 3; p++) { pinMode(EN_PINS[p],  OUTPUT); digitalWrite(EN_PINS[p],  LOW); }
}

// ── Init ─────────────────────────────────────────────────────
void pwm_init(uint8_t khz, bool perceptual) {
    s_freq_hz    = pwm_khz_ok(khz) ? (uint32_t)khz * 1000u : PWM_DEFAULT_HZ;
    s_perceptual = perceptual;

    // Set up all six channels together so the pairs land on timers 0-2.
    bool ok = true;
    for (uint8_t i = 0; i < 6; i++) ok &= ledcSetup(i, s_freq_hz, PWM_BITS) != 0;
    if (!ok && s_freq_hz != PWM_DEFAULT_HZ) {
        Serial.printf("[PWM] %lu Hz not achievable; falling back to %d Hz\n",
                      (unsigned long)s_freq_hz, PWM_DEFAULT_HZ);
        s_freq_hz = PWM_DEFAULT_HZ;
        for (uint8_t i = 0; i < 6; i++) ledcSetup(i, s_freq_hz, PWM_BITS);
    }
    for (uint8_t i = 0; i < 6; i++) ledcAttachPin(PWM_PINS[i], i);
    for (uint8_t i = 0; i < 6; i++) ledcWrite(i, 0);

    uint32_t now = millis();
    for (uint8_t p = 0; p < 3; p++) {
        pinMode(EN_PINS[p], OUTPUT);
        digitalWrite(EN_PINS[p], LOW);
        s_out[p] = PanelOut{ {0, 0}, false, false, now, now, now, 0xFF, 0 };
    }
    Serial.printf("[PWM] 6-ch @ %lu Hz/%d-bit, %s curve, high-side enables 15/16/21. All OFF.\n",
                  (unsigned long)s_freq_hz, PWM_BITS, s_perceptual ? "perceptual" : "linear");
}

bool pwm_khz_ok(uint8_t khz) { return khz == 20 || khz == 25 || khz == 30 || khz == 39; }

bool pwm_set_freq_khz(uint8_t khz) {
    if (!pwm_khz_ok(khz)) return false;
    uint32_t hz = (uint32_t)khz * 1000u;
    if (hz == s_freq_hz) return true;
    // ch0/1, ch2/3, ch4/5 each share a timer; reconfiguring keeps the duty
    // registers, so panels stay lit through the change.
    bool ok = true;
    for (uint8_t i = 0; i < 6; i += 2) ok &= ledcChangeFrequency(i, hz, PWM_BITS) != 0;
    if (!ok) {   // put every timer back where it was; caller does not save
        for (uint8_t i = 0; i < 6; i += 2) ledcChangeFrequency(i, s_freq_hz, PWM_BITS);
        Serial.printf("[PWM] %lu Hz rejected by LEDC; staying at %lu Hz\n",
                      (unsigned long)hz, (unsigned long)s_freq_hz);
        hz = s_freq_hz;
    }
    // Duty registers are in counts, which are unchanged at the same bit depth.
    for (uint8_t p = 0; p < 3; p++) {
        ledcWrite(ledc_ch(p, 0), s_out[p].duty[0]);
        ledcWrite(ledc_ch(p, 1), s_out[p].duty[1]);
    }
    if (!ok) return false;
    s_freq_hz = hz;
    Serial.printf("[PWM] frequency now %lu Hz\n", (unsigned long)hz);
    return true;
}

uint32_t pwm_freq_hz() { return s_freq_hz; }

void pwm_set_curve(bool perceptual) { s_perceptual = perceptual; }
bool pwm_curve_perceptual()         { return s_perceptual; }

float pwm_level(uint8_t brightness_pct) {
    if (brightness_pct == 0) return 0.0f;
    if (brightness_pct >= 100) return 1.0f;
    float pct = brightness_pct;
    if (!s_perceptual) return pct / 100.0f;
    float y = (pct <= 8.0f) ? pct / 903.3f : powf((pct + 16.0f) / 116.0f, 3.0f);
    return PERC_FLOOR + (1.0f - PERC_FLOOR) * y;
}

// ── All off ──────────────────────────────────────────────────
void pwm_all_off() {
    for (uint8_t p = 0; p < 3; p++) pwm_set_panel(p, 0, s_out[p].last_cct ? s_out[p].last_cct : 3200);
}

// ── Per-panel output ─────────────────────────────────────────
// brightness_pct : 0-100
// cct_k          : 2700-6500 (clamped)
void pwm_set_panel(uint8_t panel, uint8_t brightness_pct, uint16_t cct_k) {
    if (panel > 2) return;
    PanelOut &o = s_out[panel];

    if (brightness_pct > 100) brightness_pct = 100;
    if (cct_k < 2700) cct_k = 2700;
    if (cct_k > 6500) cct_k = 6500;

    float lvl  = pwm_level(brightness_pct);
    float warm = (6500.0f - cct_k) / 3800.0f;
    float cool = (cct_k  - 2700.0f) / 3800.0f;

    uint16_t pwm_w = (uint16_t)(lvl * warm * PWM_MAX + 0.5f);
    uint16_t pwm_c = (uint16_t)(lvl * cool * PWM_MAX + 0.5f);
    if (brightness_pct > 0) {
        if (warm >= 0.5f && pwm_w < PWM_MIN_COUNTS) pwm_w = PWM_MIN_COUNTS;
        if (cool >= 0.5f && pwm_c < PWM_MIN_COUNTS) pwm_c = PWM_MIN_COUNTS;
    }

    bool held = apply(panel, pwm_w, pwm_c);

    if (brightness_pct != o.last_bri || cct_k != o.last_cct) {
        VLOG("[PWM] p%d  bri=%d%%  cct=%dK  w=%u  c=%u%s\n",
             panel, brightness_pct, cct_k, (unsigned)pwm_w, (unsigned)pwm_c,
             held ? "  (held off: fault)" : "");
        o.last_bri = brightness_pct;
        o.last_cct = cct_k;
    }
}

void pwm_set_channels_frac(uint8_t panel, float warm, float cool) {
    if (panel > 2) return;
    if (warm < 0) warm = 0; if (warm > 1) warm = 1;
    if (cool < 0) cool = 0; if (cool > 1) cool = 1;
    apply(panel, (uint16_t)(warm * PWM_MAX + 0.5f), (uint16_t)(cool * PWM_MAX + 0.5f));
    s_out[panel].last_bri = 0xFF;   // next pwm_set_panel always logs
}

void pwm_cut_panel(uint8_t panel) {
    if (panel > 2) return;
    duty_write(panel, 0, 0);
    hs_write(panel, false);
}

void pwm_tick() {
    uint32_t now = millis();
    for (uint8_t p = 0; p < 3; p++) {
        PanelOut &o = s_out[p];
        if (o.hs_on && o.duty[0] == 0 && o.duty[1] == 0 &&
            now - o.zero_since_ms >= HS_OFF_DELAY_MS &&
            now - o.hs_changed_ms >= HS_OFF_DELAY_MS) {
            hs_write(p, false);
        }
    }
}

void pwm_selftest_enable(uint8_t panel, bool on) {
    if (panel > 2) return;
    duty_write(panel, 0, 0);
    hs_write(panel, on);
}

// ── Queries ──────────────────────────────────────────────────
uint16_t pwm_duty(uint8_t p, uint8_t ch)  { return (p < 3 && ch < 2) ? s_out[p].duty[ch] : 0; }
float    pwm_duty_frac(uint8_t p, uint8_t ch) { return pwm_duty(p, ch) / (float)PWM_MAX; }
uint16_t pwm_max()                        { return PWM_MAX; }
bool     pwm_hs_on(uint8_t p)             { return p < 3 && s_out[p].hs_on; }
uint32_t pwm_hs_changed_ms(uint8_t p)     { return p < 3 ? s_out[p].hs_changed_ms : 0; }
uint32_t pwm_duty_changed_ms(uint8_t p)   { return p < 3 ? s_out[p].duty_changed_ms : 0; }
bool     pwm_demand(uint8_t p)            { return p < 3 && s_out[p].demand; }
