#include "drivers/ext_clock.h"
#include "main_constants.h" // For millis declaration and timing constants like MAX_INTERVAL_DIFFERENCE, EXT_CLOCK_TIMEOUT_MS
#include "variables.h"      // For timing constants like MIN_INTERVAL, MAX_INTERVAL

#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/exti.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/syscfg.h> // Required for SYSCFG clock enable (F4)
#include <stdint.h>
#include <stdbool.h>
#include <limits.h> // For UINT32_MAX

// --- Configuration ---
#define EXT_CLOCK_PORT GPIOB
#define EXT_CLOCK_PIN GPIO3
#define EXT_CLOCK_EXTI EXTI3
#define EXT_CLOCK_NVIC_IRQ NVIC_EXTI3_IRQ // EXTI3 has its own IRQ

// Define min/max intervals (for basic validation)
// Using MIN_INTERVAL and MAX_INTERVAL from main_constants.h for consistency
// #define MIN_EXT_CLOCK_INTERVAL_MS 20 // Use MIN_INTERVAL from variables.h
// #define MAX_EXT_CLOCK_INTERVAL_MS 3000 // Use MAX_INTERVAL from variables.h

// Validation settings
#define NUM_EXT_INTERVALS_FOR_VALIDATION 3 // Number of stable intervals needed
// Use MAX_INTERVAL_DIFFERENCE from main_constants.h for stability threshold
#define EXT_CLOCK_DEBOUNCE_MS 5 // Debounce time in ms

// --- Internal State ---
static volatile uint32_t last_pulse_time_ms = 0; // Time of the last valid pulse start (after debounce)
static volatile uint32_t last_isr_time_ms = 0;   // Time of the last ISR execution (after debounce)
static volatile uint32_t validated_interval_ms = 0; // Last validated stable interval
static volatile uint32_t last_validated_event_time_ms = 0; // Timestamp of the validating pulse
static volatile bool g_ext_clock_validated_interval_ready = false; // Flag for input_handler to check

// Validation buffer state
static uint32_t ext_intervals[NUM_EXT_INTERVALS_FOR_VALIDATION] = {0};
static uint8_t ext_interval_index = 0;


// ISR for EXTI3 (this file handles its own ISR)
void exti3_isr(void) {
    if (exti_get_flag_status(EXT_CLOCK_EXTI)) {
        ext_clock_handle_irq(); // Call the handler
        exti_reset_request(EXT_CLOCK_EXTI); // Clear the interrupt flag
    }
}

// Reset helper for validation buffer
static void reset_ext_clock_validation_buffer(void) {
    ext_interval_index = 0;
    for (int i = 0; i < NUM_EXT_INTERVALS_FOR_VALIDATION; i++) {
        ext_intervals[i] = 0;
    }
    // Don't reset validated_interval_ms here; keep the last known good one until a new one is found.
}


/**
 * @brief Actual handler logic called by the ISR. Handles debounce and validation.
 */
void ext_clock_handle_irq(void) {
    uint32_t now = millis();

    // Basic Debounce: Ignore if interrupt happens too quickly after the last one.
    if (now - last_isr_time_ms < EXT_CLOCK_DEBOUNCE_MS) {
        return; // Ignore bounce
    }
    // Record time of this (debounced) ISR execution *before* checking pin state
    last_isr_time_ms = now; 

    // Only process RISING edges (check pin state AFTER debounce period)
    if (gpio_get(EXT_CLOCK_PORT, EXT_CLOCK_PIN)) {
        
        if (last_pulse_time_ms != 0) {
            // Calculate interval from the *start* of the last valid pulse
            uint32_t interval = now - last_pulse_time_ms;

            // Basic interval range check (using defines from variables.h)
            if (interval >= MIN_INTERVAL && interval <= MAX_INTERVAL) {
                // Store valid interval in the buffer
                ext_intervals[ext_interval_index++] = interval;

                // Check if buffer is full
                if (ext_interval_index >= NUM_EXT_INTERVALS_FOR_VALIDATION) {
                    // Buffer full, check for stability
                    uint64_t interval_sum = 0;
                    uint32_t min_val = UINT32_MAX, max_val = 0;
                    for (int i = 0; i < NUM_EXT_INTERVALS_FOR_VALIDATION; i++) {
                        interval_sum += ext_intervals[i];
                        if (ext_intervals[i] < min_val) min_val = ext_intervals[i];
                        if (ext_intervals[i] > max_val) max_val = ext_intervals[i];
                    }

                    // Use MAX_INTERVAL_DIFFERENCE from main_constants.h for stability check
                    if ((max_val - min_val) <= MAX_INTERVAL_DIFFERENCE) {
                        // Intervals are stable, calculate average
                        uint32_t avg_interval = (uint32_t)(interval_sum / NUM_EXT_INTERVALS_FOR_VALIDATION);

                        // Sanity check average against absolute min/max
                        if (avg_interval < MIN_INTERVAL) avg_interval = MIN_INTERVAL;
                        if (avg_interval > MAX_INTERVAL) avg_interval = MAX_INTERVAL;

                        // Atomically update the validated interval and set the flag
                        // Check if the new validated interval is actually different from the current one
                        // to avoid unnecessary signaling.
                        if (avg_interval != validated_interval_ms) {
                             nvic_disable_irq(EXT_CLOCK_NVIC_IRQ); // Protect access
                             validated_interval_ms = avg_interval;
                             last_validated_event_time_ms = now; // Store timestamp of the validating pulse
                             g_ext_clock_validated_interval_ready = true; // Signal input_handler
                             nvic_enable_irq(EXT_CLOCK_NVIC_IRQ);
                        }
                    }
                    // Reset buffer regardless of stability to start collecting next set
                    reset_ext_clock_validation_buffer();
                }
            } else {
                // Interval out of range, reset validation process
                reset_ext_clock_validation_buffer();
                // Consider resetting validated_interval_ms to 0? Or keep last good one? Keep last good for now.
            }
        }
        // Record the time of this valid pulse start *after* processing interval
        last_pulse_time_ms = now;
    }
    // Ignore FALLING edges
}


/**
 * @brief Initializes the External Clock input pin (PB3) and EXTI settings.
 */
void ext_clock_init(void) {
    rcc_periph_clock_enable(RCC_GPIOB); // Clock for GPIOB
    rcc_periph_clock_enable(RCC_SYSCFG); // Clock for SYSCFG (needed for EXTI config on F4)

    gpio_mode_setup(EXT_CLOCK_PORT, GPIO_MODE_INPUT, GPIO_PUPD_NONE, EXT_CLOCK_PIN); // Configure PB3 as input, no pull

    nvic_enable_irq(EXT_CLOCK_NVIC_IRQ);
    exti_select_source(EXT_CLOCK_EXTI, EXT_CLOCK_PORT); // Map EXTI3 to GPIOB
    // Trigger on RISING edge only
    exti_set_trigger(EXT_CLOCK_EXTI, EXTI_TRIGGER_RISING);
    exti_enable_request(EXT_CLOCK_EXTI);

    // Reset state
    last_pulse_time_ms = 0;
    last_isr_time_ms = 0;
    validated_interval_ms = 0;
    last_validated_event_time_ms = 0;
    g_ext_clock_validated_interval_ready = false;
    reset_ext_clock_validation_buffer();
}


/**
 * @brief Checks if a new validated external clock interval is ready since the last check.
 *        Clears the flag if it was set.
 *
 * @return true If a new validated interval is ready, false otherwise.
 */
bool ext_clock_has_new_validated_interval(void) {
    bool ready;
    nvic_disable_irq(EXT_CLOCK_NVIC_IRQ);
    ready = g_ext_clock_validated_interval_ready;
    g_ext_clock_validated_interval_ready = false; // Clear the flag atomically
    nvic_enable_irq(EXT_CLOCK_NVIC_IRQ);
    return ready;
}

/**
 * @brief Gets the last validated stable interval measured between rising edges.
 *        This interval is only updated when a sequence of stable intervals is detected.
 *
 * @return uint32_t The last validated interval in ms, or 0 if none validated yet or clock timed out.
 */
uint32_t ext_clock_get_validated_interval(void) {
    uint32_t interval;
    // Check for timeout first. If timed out, the validated interval is no longer current.
    if (ext_clock_has_timed_out(millis())) { // Check timeout against current time
         return 0; 
    }
    nvic_disable_irq(EXT_CLOCK_NVIC_IRQ);
    interval = validated_interval_ms;
    nvic_enable_irq(EXT_CLOCK_NVIC_IRQ);
    return interval;
}

/**
 * @brief Gets the timestamp (in ms) of the rising edge of the external clock pulse
 *        that completed the most recent interval validation.
 *
 * @return uint32_t The timestamp of the validating event, or 0 if no interval
 *                  has been validated yet or if the clock has timed out.
 */
uint32_t ext_clock_get_last_validated_event_time(void) {
    uint32_t event_time;
    if (ext_clock_has_timed_out(millis())) {
        return 0;
    }
    nvic_disable_irq(EXT_CLOCK_NVIC_IRQ);
    event_time = last_validated_event_time_ms;
    nvic_enable_irq(EXT_CLOCK_NVIC_IRQ);
    return event_time;
}


/**
 * @brief Checks if the external clock has stopped sending pulses (timed out).
 *        Timeout is based on the last *raw* ISR execution time (after debounce),
 *        as this indicates the last known activity.
 *
 * @param current_time_ms The current system time.
 * @return true If the time since the last detected pulse exceeds EXT_CLOCK_TIMEOUT_MS, false otherwise.
 */
bool ext_clock_has_timed_out(uint32_t current_time_ms) {
    uint32_t last_activity;
    nvic_disable_irq(EXT_CLOCK_NVIC_IRQ);
    // Use last_isr_time_ms for timeout, as it reflects the last time we *knew* the clock was active
    last_activity = last_isr_time_ms;
    nvic_enable_irq(EXT_CLOCK_NVIC_IRQ);

    // Handle potential initial state before any ISR has run
    if (last_activity == 0) {
        return true; // Assume timed out if no pulse ever detected
    }

    // Use EXT_CLOCK_TIMEOUT_MS from main_constants.h
    return (current_time_ms > last_activity + EXT_CLOCK_TIMEOUT_MS); 
}
