// SPDX-FileCopyrightText: 2026 DrEvan
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
/*
 * sense_config.h - Current/rail sensing constants and fault thresholds (PCB Rev 0.1)
 *
 * Sense chain (per channel):
 *   25 mΩ shunt (PA-R8/R9, UNI-ROYAL 201007F250MT4E)
 *   -> INA181A2, gain 50 V/V (TI SBOS793)
 *   -> ADS1115 single-ended, PGA ±4.096 V (TI SBAS444E Table 8-3)
 *   = 1.25 V/A at the ADC. INA output saturates near 3.25 V (~2.6 A).
 *
 * Panel rail: 100k/10k divider (PA-R12/PA-R13) -> ADS1115 AIN2, ×11.
 *
 * The delta-sigma ADC averages the 25 kHz PWM waveform, so a channel reading
 * is average current = duty × on-current.
 */

// ── ADS1115 ──────────────────────────────────────────────────
#define ADS_ADDR_A            0x48   // PA-U3 ADDR -> GND
#define ADS_ADDR_B            0x49   // PB-U3 ADDR -> VDD
#define ADS_ADDR_C            0x4A   // PC-U3 ADDR -> SDA
#define ADS_FSR_V             4.096f // PGA 001b
#define ADS_DR_BITS           0b101  // 250 SPS (4 ms conversion, ~100 PWM cycles averaged)
#define ADS_CONV_WAIT_MS      7      // 1/DR = 4 ms, +10% oscillator tolerance, + margin
                                     // (timed from after the last start write)

#define I2C_BUS_HZ            100000 // 100 kHz standard mode

// ── Scaling ──────────────────────────────────────────────────
#define SHUNT_OHMS            0.025f   // Ω
#define INA_GAIN              50.0f    // V/V, INA181A2
#define AMPS_PER_VOLT         (1.0f / (SHUNT_OHMS * INA_GAIN))   // 0.8 A/V
#define RAIL_DIV_RATIO        11.0f                              // (100k + 10k) / 10k

// ── Fault thresholds ─────────────────────────────────────────
// Over-current limit, per port, applied to both channels:
//   Manual override (g_state.oc_ma[p] != 0)  -> that value
//   Auto, Panel Check reference saved         -> OC_REF_MULT x reference,
//                                                 clamped OC_AUTO_MIN_A..OC_MAX_A
//   Auto, no reference                         -> OC_DEFAULT_A
// Compared against the estimated on-current (avg / duty, duty >= OC_MIN_DUTY)
// and against the average current at any duty. The per-panel fuse is the
// primary protection; this catches partial shorts the fuse tolerates.
#define OC_REF_MULT           2.0f   // multiple of the saved reference
#define OC_AUTO_MIN_A         0.40f  // floor for small panels (noise margin)
#define OC_DEFAULT_A          1.80f  // no reference yet: above a ~1.5 A panel
#define OC_MAX_A              2.40f  // below the ~2.6 A sense ceiling (INA181 swing)
#define OC_MANUAL_MIN_MA      200    // manual override range, mA
#define OC_MANUAL_MAX_MA      2400   // mA
#define OC_MANUAL_STEP_MA     50     // mA
#define OC_MIN_DUTY           0.10f  // below this, avg / duty is too noisy to use

#define STUCK_ON_A            0.050f // stuck-on: current above this at 0 duty

// Open channel: duty >= OPEN_MIN_DUTY with current below OPEN_MAX_A (a healthy
// channel draws ~0.048 A at 15 % duty). Thresholds are in duty, i.e. after the
// brightness curve, not in brightness percent.
#define OPEN_MIN_DUTY         0.15f  // duty fraction
#define OPEN_MAX_A            0.010f // A
// Open panel: warm + cool duty >= this and total current < 2 x OPEN_MAX_A.
// Catches a disconnected panel at any CCT, where one channel may be too dim
// for the per-channel check.
#define PANEL_OPEN_MIN_DUTY   0.15f  // duty fraction

// Panel rail (24 V nominal) while the high-side switch is ON:
#define RAIL_FUSE_OPEN_V      12.0f  // below this -> fuse open / high-side failed
#define RAIL_LOW_V            20.0f  // below this -> low supply warning
// Panel rail while the high-side switch is OFF (after discharge holdoff):
#define RAIL_STUCK_HS_V       8.0f   // above this -> high-side P-FET stuck on

#define ZERO_MAX_A            0.030f // auto-zero offsets above this are rejected

// ── Timing ───────────────────────────────────────────────────
#define SETTLE_AFTER_CHANGE_MS   120   // ignore current checks after a duty change
#define SETTLE_AFTER_ENABLE_MS   150   // ignore rail checks after high-side turns on
#define HS_DISCHARGE_HOLDOFF_MS 5000   // rail bleed-down after high-side turns off
                                       // (110 µF vs ~9 kΩ divider/LED load, tau ~1 s)
#define HS_OFF_DELAY_MS         2000   // high-side stays on this long after demand hits 0
#define DEBOUNCE_SWEEPS            3   // consecutive sweeps a condition must persist
#define DEBOUNCE_SWEEPS_OC         2   // over-current debounces faster
#define SENSE_STALE_MS           500   // panel sense marked offline if no sweep this long

// ── Boot self-test ───────────────────────────────────────────
#define SELFTEST_ENABLE            1   // run the self-test at boot
#define SELFTEST_SETTLE_MS       250   // high-side on, PWM 0, before reading

// ── Bring-up tools ───────────────────────────────────────────
// Fuse turn-on cycling test (Menu > Fuse Cycle Test). Checks that inrush into
// the bulk capacitors does not fatigue the panel fuse. Set to 1 to build it in;
// rerun after any fuse or bulk-capacitor change.
#define ENABLE_FUSE_CYCLE_TEST     0
#define FUSE_TEST_PANEL            0      // Panel A
#define FUSE_TEST_CYCLES          40      // turn-on cycles
#define FUSE_TEST_BRI            100      // ON phase drives the panel like a real turn-on
#define FUSE_TEST_CCT           4600      // 50/50 warm/cool
#define FUSE_TEST_MIN_A        0.10f     // total panel current must exceed this at end of ON
#define FUSE_TEST_ON_MS         1500      // panel lit per cycle
#define FUSE_TEST_OFF_MS        5000      // off: ~5 tau bleed of the 110 uF bank
#define FUSE_TEST_RAIL_OK_V       20.0f   // rail must read above this at end of ON

// ── Panel Check (Diagnostics > Check) ────────────────────────
// Lights each channel alone at PANELCHECK_DUTY. Each port stores its own
// reference from a known-good panel, so no panel size is assumed.
// With a reference: pass within ±PANELCHECK_REF_TOL. Without: structural checks
// only (current flows, other channel dark, no trip).
#define PANELCHECK_DUTY          0.30f   // duty fraction per channel
#define PANELCHECK_REF_TOL       0.25f   // ±fraction around the saved reference
#define PANELCHECK_REF_MAX_MA    3000    // sanity cap on a stored full-duty reference, mA
#define PANELCHECK_XTALK_A       0.020f  // the unlit channel must stay below this
#define PANELCHECK_SETTLE_MS     600     // after each step, before averaging
#define PANELCHECK_MEASURE_MS    600     // averaging window
