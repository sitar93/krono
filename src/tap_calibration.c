#include "tap_calibration.h"

#include <stdbool.h>
#include <stdint.h>

#include <libopencm3/stm32/gpio.h>

#include "drivers/io.h"
#include "drivers/tap.h"
#include "drivers/tap_calib_store.h"
#include "main_constants.h"
#include "variables.h"

#define CAL_TOTAL_TAPS 16u
#define CAL_WARMUP_TAPS 4u
#define CAL_MEASURED (CAL_TOTAL_TAPS - CAL_WARMUP_TAPS)

/**
 * MOD button level on this PCB: pressed = PA1 HIGH (verified on hardware; the external pull-down wins over
 * the MCU pull-up used elsewhere, so the main input code effectively sees an inverted flag).
 */
static bool mod_pressed(void) {
    return gpio_get(GPIOA, GPIO1) != 0u;
}

static void leds(bool status_on, bool aux_on) {
    set_output(JACK_OUT_STATUS_LED_PA15, status_on);
    set_output(JACK_OUT_AUX_LED_PA3, aux_on);
}

static void wait_ms(uint32_t ms) {
    uint32_t const t0 = millis();
    while ((millis() - t0) < ms) {
    }
}

/** Debounced MOD press during the procedure (cancel). */
static bool mod_cancel_requested(void) {
    static uint32_t pressed_since = 0;
    static bool tracking = false;
    if (mod_pressed()) {
        if (!tracking) {
            tracking = true;
            pressed_since = millis();
        } else if ((millis() - pressed_since) >= 40u) {
            return true;
        }
    } else {
        tracking = false;
    }
    return false;
}

static void blink_both(uint32_t times, uint32_t on_ms, uint32_t off_ms) {
    for (uint32_t i = 0; i < times; i++) {
        leds(true, true);
        wait_ms(on_ms);
        leds(false, false);
        wait_ms(off_ms);
    }
}

/**
 * One measurement round. Returns 1 = success (offset saved), 0 = too irregular (retry), -1 = cancelled.
 */
static int32_t calibration_round(void) {
    uint32_t const period = 60000u / TAP_CALIB_BPM;
    int32_t asyn[CAL_MEASURED];
    uint32_t n_meas = 0;
    uint32_t taps = 0;
    uint32_t ack_off_ms = 0;
    bool ack_on = false;

    (void)tap_detected(); // Drop any stale tap
    (void)tap_get_interval();

    uint32_t const c0 = millis() + 500u; // First click shortly after the round starts
    uint32_t next_click = c0;
    uint32_t const start = millis();
    leds(true, false); // Status LED on = calibration running

    while (taps < CAL_TOTAL_TAPS) {
        uint32_t const now = millis();
        if (mod_cancel_requested() || (now - start) > TAP_CALIB_TIMEOUT_MS) {
            return -1;
        }
        if (ack_on && (int32_t)(now - ack_off_ms) >= 0) {
            set_output(JACK_OUT_AUX_LED_PA3, false);
            ack_on = false;
        }
        if ((int32_t)(now - next_click) >= 0) {
            set_output_high_for_duration(JACK_OUT_1A, TAP_CALIB_CLICK_MS);
            set_output_high_for_duration(JACK_OUT_1B, TAP_CALIB_CLICK_MS);
            next_click += period;
        }
        if (tap_detected()) {
            uint32_t const t = tap_get_last_press_time_ms();
            (void)tap_get_interval();
            if ((int32_t)(t - c0) < -(int32_t)(period / 2u)) {
                continue; // Tap before the first click: ignore
            }
            // Asynchrony to the nearest click (negative = tap before the click)
            int32_t const rel = (int32_t)(t - c0);
            int32_t const k = (rel + (int32_t)(period / 2u)) / (int32_t)period;
            int32_t const a = rel - k * (int32_t)period;
            taps++;
            set_output(JACK_OUT_AUX_LED_PA3, true); // Short tap acknowledgement (non-blocking)
            ack_off_ms = now + 40u;
            ack_on = true;
            if (taps > CAL_WARMUP_TAPS && n_meas < CAL_MEASURED) {
                asyn[n_meas++] = a;
            }
        }
    }

    /*
     * Robust estimate. The player's asynchrony must be roughly constant: if it drifts (tapping at a tempo different
     * from the click) its mean measures the tempo error, not the latency, so the round fails.
     *  1. Line fit asynchrony vs tap index; drop taps further than TAP_CALIB_OUTLIER_MS from the line; refit.
     *  2. Fail if the slope exceeds TAP_CALIB_MAX_DRIFT_MS (ms per tap) or the residual spread is too large.
     *  3. Offset = -(mean asynchrony of the kept taps).
     */
    bool keep[CAL_MEASURED];
    for (uint32_t i = 0; i < n_meas; i++) {
        keep[i] = true;
    }
    float slope = 0.0f;
    float mean_a = 0.0f;
    uint32_t kept = 0;
    for (uint32_t pass = 0; pass < 2u; pass++) {
        float sx = 0.0f, sa = 0.0f;
        kept = 0;
        for (uint32_t i = 0; i < n_meas; i++) {
            if (keep[i]) {
                sx += (float)i;
                sa += (float)asyn[i];
                kept++;
            }
        }
        if (kept < (CAL_MEASURED * 2u) / 3u) {
            return 0;
        }
        float const mx = sx / (float)kept;
        mean_a = sa / (float)kept;
        float sxx = 0.0f, sxa = 0.0f;
        for (uint32_t i = 0; i < n_meas; i++) {
            if (keep[i]) {
                float const dx = (float)i - mx;
                sxx += dx * dx;
                sxa += dx * ((float)asyn[i] - mean_a);
            }
        }
        slope = (sxx > 0.0f) ? (sxa / sxx) : 0.0f;
        if (pass == 0u) {
            for (uint32_t i = 0; i < n_meas; i++) {
                float const r = (float)asyn[i] - (mean_a + slope * ((float)i - mx));
                keep[i] = (r <= (float)TAP_CALIB_OUTLIER_MS) && (r >= -(float)TAP_CALIB_OUTLIER_MS);
            }
        } else {
            float spread = 0.0f; // mean absolute residual
            for (uint32_t i = 0; i < n_meas; i++) {
                if (keep[i]) {
                    float const r = (float)asyn[i] - (mean_a + slope * ((float)i - mx));
                    spread += (r < 0.0f) ? -r : r;
                }
            }
            spread /= (float)kept;
            if (spread > (float)TAP_CALIB_MAX_SPREAD_MS) {
                return 0;
            }
        }
    }
    if (slope > (float)TAP_CALIB_MAX_DRIFT_MS || slope < -(float)TAP_CALIB_MAX_DRIFT_MS) {
        return 0; // Tapping at a different tempo than the click: retry
    }
    int32_t const mean = (int32_t)(mean_a + ((mean_a < 0.0f) ? -0.5f : 0.5f));

    int32_t offset = -mean; // Player taps early (mean < 0) -> place beats later
    if (offset > TAP_CALIB_MAX_OFFSET_MS) {
        offset = TAP_CALIB_MAX_OFFSET_MS;
    }
    if (offset < -TAP_CALIB_MAX_OFFSET_MS) {
        offset = -TAP_CALIB_MAX_OFFSET_MS;
    }
    leds(true, true); // Both lit while saving (blocking flash write)
    return tap_calib_save(offset) ? 1 : 0;
}

void tap_calibration_check_boot(void) {
    // Runs before input_handler_init: configure MOD (PA1) ourselves (pull-down: idle low) and let it settle
    gpio_mode_setup(GPIOA, GPIO_MODE_INPUT, GPIO_PUPD_PULLDOWN, GPIO1);
    wait_ms(20);
    if (!mod_pressed()) {
        return;
    }
    // Entry: MOD must stay held for TAP_CALIB_ENTRY_HOLD_MS (status LED flickers meanwhile)
    uint32_t const t0 = millis();
    while ((millis() - t0) < TAP_CALIB_ENTRY_HOLD_MS) {
        if (!mod_pressed()) {
            leds(false, false);
            return; // Released early: normal boot
        }
        leds(((millis() - t0) / 100u) % 2u == 0u, false);
    }

    // Entered: alternate both LEDs until MOD is released
    while (mod_pressed()) {
        bool const phase = ((millis() / 250u) % 2u) == 0u;
        leds(phase, !phase);
    }
    leds(false, false);
    tap_init(); // TAP button EXTI (input_handler_init re-initialises it afterwards)
    wait_ms(300);

    for (;;) {
        int32_t const r = calibration_round();
        if (r > 0) {
            blink_both(3u, 80u, 120u); // Success: double-ish blinks
            wait_ms(150);
            blink_both(3u, 80u, 120u);
            break;
        }
        if (r < 0) {
            blink_both(1u, 300u, 300u); // Cancelled: keep previous calibration
            break;
        }
        leds(true, true); // Too irregular: long blink, then retry
        wait_ms(1000);
        leds(false, false);
        wait_ms(600);
    }
    leds(false, false);
    while (mod_pressed()) { // Do not leak the cancel press into normal operation
    }
}
