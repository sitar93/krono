#include "status_led.h"
#include "drivers/io.h"
#include "util/delay.h"
#include "main_constants.h"
#include "variables.h"
#include <stdbool.h>
#include <libopencm3/stm32/gpio.h> // Added for Debug LED toggle

// Module state
static uint32_t last_blink_time = 0;
static bool led_state = false;
static operational_mode_t current_mode_for_led = MODE_DEFAULT;
static uint8_t blink_count = 0;        // Completed pulses in the current sequence
static uint32_t active_on_duration_ms = STATUS_LED_BASE_INTERVAL_MS; // ON time for current pulse
static uint32_t pending_off_gap_ms = STATUS_LED_INTER_PULSE_OFF_MS;  // dark time before next pulse

// --- Override State ---
static bool led_override_active = false;
static bool led_override_fixed_state = false;

/** User-facing mode number from enum index (1 = first mode). */
static uint8_t status_led_user_mode_number(operational_mode_t mode) {
    if (mode >= NUM_OPERATIONAL_MODES) {
        return 1;
    }
    return (uint8_t)mode + 1u;
}

/**
 * One loop: modes 1–9 → u× short (N). Modes ≥10 → (tens)× long (L) + (units)× short (N).
 * Examples: 10=L; 11=L+N; 19=L+9N; 20=L+L; 21=L+L+N; 30=L+L+L. Then sequence gap.
 */
static uint8_t status_led_pulse_count(operational_mode_t mode) {
    uint8_t u = status_led_user_mode_number(mode);
    if (u < 10u) {
        return u;
    }
    return (uint8_t)(u / 10u + u % 10u);
}

/** Pulse index 0..count−1: true = long ON, false = normal ON. */
static bool status_led_pulse_is_long(operational_mode_t mode, uint8_t pulse_index) {
    uint8_t u = status_led_user_mode_number(mode);
    if (u < 10u) {
        return false;
    }
    uint8_t tens = (uint8_t)(u / 10u);
    return pulse_index < tens;
}

/* Logical true = LED visibly on. Drive pin directly; older !on caused “dark” pauses to read as lit on boards where ON = GPIO high. */
static void set_led(bool on) {
    set_output(STATUS_LED_PIN, on);
    led_state = on;
}

void status_led_init(void) {
    led_override_active = false; // Ensure override is off
    status_led_reset(); // Reset calls set_led(false)
}

void status_led_set_mode(operational_mode_t mode) {
    if (mode < NUM_OPERATIONAL_MODES) {
        // Only reset if the mode actually changes AND override is not active
        if (mode != current_mode_for_led && !led_override_active) {
             current_mode_for_led = mode;
             status_led_reset();
        } else {
            // Still update the internal mode variable even if not resetting sequence
            current_mode_for_led = mode;
        }
    }
}

void status_led_reset(void) {
     // Reset counters only if not overridden
     if (!led_override_active) {
        // --- DEBUG --- Toggle PA3 LED on reset - REMOVED
        // gpio_toggle(GPIOA, GPIO3);
        // --- END DEBUG ---

        blink_count = 0;
        pending_off_gap_ms = STATUS_LED_INTER_PULSE_OFF_MS ? STATUS_LED_INTER_PULSE_OFF_MS : 1u;
        last_blink_time = millis(); // Start sequence immediately
        set_led(false); // Start with LED off
     }
}


void status_led_update(uint32_t current_time_ms) {
    // --- Check Override --- //
    if (led_override_active) {
        set_led(led_override_fixed_state);
        return;
    }

    // --- Normal Blinking Logic ---
    uint8_t const total_pulses = status_led_pulse_count(current_mode_for_led);
    /* Inverted vs older firmware: long dark gap *between* pulses; short gap *after* full pattern. */
    uint32_t const sequence_pause = STATUS_LED_SEQUENCE_GAP_MS;

    // --- State Machine for Blinking --- //

    if (blink_count >= total_pulses) {
        if (current_time_ms - last_blink_time >= sequence_pause) {
            blink_count = 0;
            pending_off_gap_ms = STATUS_LED_INTER_PULSE_OFF_MS ? STATUS_LED_INTER_PULSE_OFF_MS : 1u;
            last_blink_time = current_time_ms;
            set_led(false);
        } else {
            if (led_state) {
                set_led(false);
            }
            return;
        }
    }

    if (!led_state) {
        uint32_t const gap = pending_off_gap_ms ? pending_off_gap_ms : 1u;
        if (current_time_ms - last_blink_time >= gap) {
            active_on_duration_ms = status_led_pulse_is_long(current_mode_for_led, blink_count)
                                          ? STATUS_LED_LONG_ON_MS
                                          : STATUS_LED_BASE_INTERVAL_MS;
            if (active_on_duration_ms == 0) {
                active_on_duration_ms = 1;
            }
            set_led(true);
            last_blink_time = current_time_ms;
        }
    } else {
        if (current_time_ms - last_blink_time >= active_on_duration_ms) {
            uint8_t const finished_pulse_idx = blink_count;
            set_led(false);
            last_blink_time = current_time_ms;
            blink_count++;
            pending_off_gap_ms = status_led_pulse_is_long(current_mode_for_led, finished_pulse_idx)
                                     ? (STATUS_LED_AFTER_LONG_OFF_MS ? STATUS_LED_AFTER_LONG_OFF_MS : 1u)
                                     : (STATUS_LED_INTER_PULSE_OFF_MS ? STATUS_LED_INTER_PULSE_OFF_MS : 1u);
        }
    }
}


void status_led_set_override(bool override_active, bool fixed_state) {
    bool was_active = led_override_active;
    led_override_active = override_active;
    led_override_fixed_state = fixed_state;

    // If override is turning ON
    if (override_active && !was_active) {
        set_led(fixed_state);
        // Reset internal tracking vars for when override ends?
        // Let's not reset here, reset happens when override turns OFF
    }
    // If override is turning OFF
    else if (!override_active && was_active) {
        status_led_reset(); // Force reset to start blinking correctly
    }
    // If override state changes while active (e.g., fixed ON to fixed OFF)
    else if (override_active && was_active) {
         set_led(fixed_state); // Update to new fixed state
    }
}
