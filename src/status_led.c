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
static uint8_t blink_count = 0;        // How many blinks have occurred in the current sequence
static uint32_t sequence_start_time = 0; // When the current blink sequence started

// --- Override State ---
static bool led_override_active = false;
static bool led_override_fixed_state = false;

// Helper to turn LED on/off
static void set_led(bool on) {
    set_output(STATUS_LED_PIN, !on); // Inverted output
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
        sequence_start_time = millis();
        last_blink_time = sequence_start_time; // Start sequence immediately
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
    // Determine the number of blinks based on the mode
    uint8_t blinks = 1; // Default to 1 blink
    switch (current_mode_for_led) {
        case MODE_DEFAULT:       blinks = 1; break;
        case MODE_EUCLIDEAN:     blinks = 2; break;
        case MODE_MUSICAL:       blinks = 3; break;
        case MODE_PROBABILISTIC: blinks = 4; break;
        case MODE_SEQUENTIAL:    blinks = 5; break;
        case MODE_SWING:         blinks = 6; break;
        case MODE_POLYRHYTHM:    blinks = 7; break;
        case MODE_LOGIC:         blinks = 8; break;
        case MODE_PHASING:       blinks = 9; break;
        case MODE_CHAOS:         blinks = 10; break; // <-- Added Chaos mode
        default: blinks = 1; break; // Safe default for unknown modes
    }
    uint32_t on_duration = STATUS_LED_BASE_INTERVAL_MS;
    uint32_t off_duration = STATUS_LED_BASE_INTERVAL_MS;
    uint32_t sequence_pause = STATUS_LED_END_OFF_MS;

    if (on_duration == 0) on_duration = 1;
    if (off_duration == 0) off_duration = 1;

    // --- State Machine for Blinking --- //

    // Check if the sequence pause is over and we need to restart
    if (blink_count >= blinks) { // Use the calculated 'blinks' variable
        if (current_time_ms - last_blink_time >= sequence_pause) {
            // Restart sequence
            blink_count = 0;
            last_blink_time = current_time_ms; // Base time for first blink OFF duration
            set_led(false); // Ensure LED is off at the start
            // Don't return, proceed to check if we need to turn ON immediately
        } else {
             // Still in pause, ensure LED is off and wait
             if(led_state) { set_led(false); }
             return;
        }
    }

    // Handle blinking within the sequence
    if (!led_state) { // If LED is currently OFF
         // Check if OFF duration has passed (or if we just restarted)
        if (current_time_ms - last_blink_time >= off_duration) {
            set_led(true); // Turn ON
            last_blink_time = current_time_ms; // Mark time ON
        }
    } else { // If LED is currently ON
        // Check if ON duration has passed
        if (current_time_ms - last_blink_time >= on_duration) {
            set_led(false); // Turn OFF
            last_blink_time = current_time_ms; // Mark time OFF
            blink_count++; // Increment count after turning OFF
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
