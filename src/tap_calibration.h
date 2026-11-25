#ifndef TAP_CALIBRATION_H
#define TAP_CALIBRATION_H

/**
 * Tap latency calibration, entered by holding MOD while powering on (TAP_CALIB_ENTRY_HOLD_MS).
 *
 *  1. Both LEDs alternate: release MOD.
 *  2. 1A / 1B click at TAP_CALIB_BPM (the blue 1A LED flashes; patching 1A to a sound is even better).
 *  3. Tap TAP 16 times along: the first 4 taps are warm-up, the other 12 measure the player's average
 *     anticipation (outliers rejected).
 *  4. Success: the offset is saved in its own flash sector (survives saves and firmware updates), both LEDs
 *     double-blink three times, normal operation starts. Too irregular: both LEDs on for 1 s, try again.
 *  MOD cancels at any time (previous calibration kept); a TAP_CALIB_TIMEOUT_MS timeout cancels too.
 *
 * Call once after system_init(); returns immediately if MOD is not held at boot.
 */
void tap_calibration_check_boot(void);

#endif
