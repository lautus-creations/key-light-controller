// SPDX-FileCopyrightText: 2026 DrEvan
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

/*
 * console - USB-CDC serial diagnostics (115200, newline-terminated)
 *
 *   help              list commands
 *   sense             per-panel rail V, warm/cool A, duty, HS state
 *   stream <ms>       repeat "sense" every <ms> (min 100); "stream off" stops
 *   faults            flags + latch state per panel
 *   clear             drop all fault latches; never re-lights a panel, the
 *                     next UI change on a panel drives it again
 *   i2c               scan the shared bus (expect 0x38, 0x48, 0x49, 0x4A)
 *   ver               version, uptime, last reset, PWM settings
 *   check             start Panel Check
 *   log on | log off  verbose logging (klc_log.h)
 *   on | off [A|B|C]  system or one panel, same as the UI buttons
 *                     (fault gate still applies, change is saved)
 */

void console_tick();   // call from loop()
