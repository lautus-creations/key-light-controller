// SPDX-FileCopyrightText: 2026 DrEvan
// SPDX-License-Identifier: GPL-3.0-or-later

#include <Arduino.h>
#include <lvgl.h>
#include "display.h"
#include "touch.h"
#include "pwm.h"
#include "sense.h"
#include "fault.h"
#include "fault_ui.h"
#include "indicators.h"
#include "console.h"
#include "fuse_test.h"
#include "panel_check.h"
#include "lock.h"
#include "sysinfo.h"
#include <esp_task_wdt.h>
#include "storage.h"
#include "app_state.h"
#include "version.h"
#include "screens/screen_manager.h"

// Loop watchdog: if loop() stops running for this long (I2C hang, LVGL lock-up),
// the chip resets. Panels go dark on reset (hardware pulldowns on every
// high-side enable) and come back from NVS on boot.
#define LOOP_WDT_TIMEOUT_S  8

// Debounced save: fires 1 s after the last state change.
static lv_timer_t *s_save_timer = nullptr;

static void save_cb(lv_timer_t *t) {
    storage_save();
    lv_timer_del(s_save_timer);
    s_save_timer = nullptr;
}

void schedule_save() {
    if (s_save_timer) {
        lv_timer_reset(s_save_timer);
    } else {
        s_save_timer = lv_timer_create(save_cb, 1000, nullptr);
        lv_timer_set_repeat_count(s_save_timer, 1);
    }
}

// Drive every port from saved state. A port that was Off before power-down
// boots dark; inactive ports are forced to zero.
void outputs_apply_all() {
    for (int i = 0; i < APP_MAX_PORTS; i++) {
        uint8_t level = (g_state.port_active[i] && g_state.system_on && g_state.panel_on[i])
                        ? g_state.brightness[i] : 0;
        pwm_set_panel((uint8_t)i, level, g_state.cct[i]);
    }
}

/*
 * Boot sequence:
 *   1. pwm_preinit_safe()  all PWM + high-side enable pins LOW (hardware
 *                          pulldowns already hold them; this re-affirms it)
 *   2. ind_init()          status LED on = "booting"
 *   3. Serial (USB CDC)
 *   4. storage_init()      load g_state from NVS
 *   5. pwm_init()          LEDC ch0-5 set up together, all zero
 *   6. i2c_bus_init()      shared bus @ 100 kHz (touch + 3x ADS1115)
 *   7. sense_init()        probe ADCs
 *   8. fault_selftest()    each active panel: high-side on, PWM 0, check rail
 *                          is live and no current flows; auto-zero sense amps
 *   9. outputs_apply_all() restore panels (tripped panels held dark)
 *  10. display_init(), touch_init(), splash, fault UI, backlight fade-in
 *  11. loop watchdog armed (LOOP_WDT_TIMEOUT_S)
 */
void setup() {
    pwm_preinit_safe();
    ind_init();

    Serial.begin(115200);
    uint32_t t = millis();
    while (!Serial && millis() - t < 500) delay(10);
    Serial.printf("\n=== Key Light Controller v%s (%s) ===\n", FW_VERSION, FW_BUILD);
    Serial.printf("Heap: %u  PSRAM: %u\n", ESP.getFreeHeap(), ESP.getFreePsram());
    sysinfo_init();
    Serial.printf("Reset reason: %s%s\n", sysinfo_reset_reason(),
                  sysinfo_reset_abnormal() ? "  <-- abnormal" : "");

    storage_init();
    pwm_init(g_state.pwm_khz, g_state.dim_curve != 0);

    i2c_bus_init();
    fault_init();
    sense_init();
    fault_selftest();

    outputs_apply_all();

    display_init();   // TFT + LVGL (backlight dark until the fade-in below)
    // Fixed screen policy: full brightness, backlight off after 1 min idle,
    // first touch turns it back on.
    display_set_level(100);
    display_set_autodim(1);
    bool touch_ok = touch_init();
    ind_set_touch_missing(!touch_ok);
    {   // Log the shared bus after the touch reset (expect 0x38 0x48 0x49 0x4A)
        uint8_t found[16];
        uint8_t n = sense_i2c_scan(found, sizeof(found));
        Serial.printf("[I2C] Scan: %d device(s):", n);
        for (uint8_t i = 0; i < n; i++) Serial.printf(" 0x%02X", found[i]);
        Serial.println();
    }

    screen_manager_goto(SCR_SPLASH);
    fault_ui_init();
    lv_timer_handler();          // render the first splash frame while still dark
    display_fade_in(600);

    ind_boot_done();

    esp_task_wdt_init(LOOP_WDT_TIMEOUT_S, true);   // reconfigures the core's TWDT (panic -> reset)
    enableLoopWDT();                               // loop task fed by the core before each loop()
    Serial.println("Boot complete. Type \"help\" for diagnostics.");
}

void loop() {
    lv_timer_handler();   // UI + touch (I2C)
    sense_poll();         // ADS1115 sweep (I2C), runs fault_evaluate() per sweep
    fault_tick();
#if ENABLE_FUSE_CYCLE_TEST
    fuse_test_tick();     // fuse cycling test (compiled out in release)
#endif
    panel_check_tick();   // Diagnostics > Check (no-op when idle)
    display_tick();       // backlight fades + auto-dim
    pwm_tick();           // deferred high-side off
    ind_tick();
    console_tick();
    delay(2);
}
