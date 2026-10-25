// SPDX-FileCopyrightText: 2026 DrEvan
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <stdbool.h>

/*
 * Screen lock: stops accidental taps from changing the lights mid-shoot.
 *
 * Lock from the Lock button on the main screen. While locked, a transparent
 * layer over the whole UI swallows touches; a tap shows a hint, and pressing
 * and holding anywhere for LOCK_HOLD_MS unlocks (a bar fills while holding).
 * Fault popups still appear above the lock so OK / Reset stay reachable.
 * Not saved: a reboot always starts unlocked.
 */
#define LOCK_HOLD_MS  1500

void lock_engage();
void lock_release();
bool lock_active();
