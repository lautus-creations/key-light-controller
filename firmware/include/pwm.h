// SPDX-FileCopyrightText: 2026 DrEvan
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <stdint.h>

/*
 * Panel output driver. The only code that touches the output stage.
 *
 * Per panel:
 *   - high-side P-FET enable (GPIO 15/16/21)
 *   - two low-side PWM channels (LEDC ch0-5, 10-bit, 20/25/30/39 kHz)
 *
 * All output goes through pwm_set_panel() / pwm_all_off(), so high-side
 * sequencing and fault gating apply everywhere.
 *
 * High-side policy:
 *   - ON as soon as a panel is asked for non-zero output.
 *   - OFF once demand has been zero for HS_OFF_DELAY_MS, so the panel bulk
 *     cap is not recharged on every slider pass through 0 %.
 *   - OFF immediately on pwm_cut_panel() (fault response).
 *
 * Fault gating: while fault.cpp holds a latched trip on a panel, output
 * requests for that panel are forced to zero. The trip re-arms when the
 * panel is commanded off and then on again (see fault.h).
 */

#define PWM_CH_WARM 0
#define PWM_CH_COOL 1

// Drive every output-stage GPIO low. First line of setup(), before Serial.
void pwm_preinit_safe();

void pwm_init(uint8_t khz, bool perceptual);       // from g_state.pwm_khz / dim_curve
void pwm_set_panel(uint8_t panel, uint8_t brightness_pct, uint16_t cct_k);
void pwm_all_off();

// Direct per-channel drive (0.0-1.0 duty each), bypassing brightness curve and
// CCT mix. Still fault-gated and high-side sequenced. Panel Check only.
void pwm_set_channels_frac(uint8_t panel, float warm, float cool);

// Settings. Callers re-apply outputs (outputs_apply_all) after a curve change.
bool     pwm_khz_ok(uint8_t khz);                   // 20, 25, 30, 39 (10-bit max ~39 kHz)
bool     pwm_set_freq_khz(uint8_t khz);              // live change, panels stay lit
uint32_t pwm_freq_hz();
void     pwm_set_curve(bool perceptual);
bool     pwm_curve_perceptual();
float    pwm_level(uint8_t brightness_pct);          // curve output 0.0-1.0

// Immediate hard cut: PWM to 0 and high-side off. Used by the fault layer.
void pwm_cut_panel(uint8_t panel);

// Housekeeping (deferred high-side off). Call from loop().
void pwm_tick();

// ── Self-test hooks (boot only) ─────────────────────────────
void pwm_selftest_enable(uint8_t panel, bool on);   // high-side only, PWM stays 0

// ── State queries for the sense / fault layer ───────────────
uint16_t pwm_duty(uint8_t panel, uint8_t ch);       // counts as written to LEDC (0..pwm_max())
float    pwm_duty_frac(uint8_t panel, uint8_t ch);  // 0.0-1.0
uint16_t pwm_max();                                 // 1023 (10-bit)
bool     pwm_hs_on(uint8_t panel);
uint32_t pwm_hs_changed_ms(uint8_t panel);          // millis() of last enable edge
uint32_t pwm_duty_changed_ms(uint8_t panel);        // millis() of last duty change
bool     pwm_demand(uint8_t panel);                 // UI is asking for output (pre-gating)
