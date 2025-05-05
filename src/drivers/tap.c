		#include "tap.h"
		#include "../main_constants.h" // For TAP_TIMEOUT_MS, DEBOUNCE_DELAY_MS
		#include "../util/delay.h" // For millis()
		#include <libopencm3/stm32/rcc.h>   // For RCC defines
		#include <libopencm3/stm32/gpio.h>
		#include <libopencm3/stm32/exti.h>
		#include <libopencm3/cm3/nvic.h>
		#include <libopencm3/stm32/syscfg.h> // For SYSCFG clock
		#include <stdbool.h>
		
		// External function from main.c to get system time
		extern uint32_t millis(void);
		
		static volatile uint32_t last_tap_time = 0;
		static volatile uint32_t tap_interval = 0;
		static volatile bool tap_detected_flag = false;
		static volatile bool first_tap_registered = false; // Flag to handle the very first tap
		
		/**
		 * @brief Initialize tap detection on PA0 using external interrupt.
		 */
		void tap_init(void) {
		    // Enable clocks (moved here for clarity)
		    rcc_periph_clock_enable(RCC_GPIOA);
		    rcc_periph_clock_enable(RCC_SYSCFG);

		    // Configure PA0 as input with pull-up resistor.
		    // Button connects to GND. Input is HIGH when idle, LOW when pressed.
		    gpio_mode_setup(GPIOA, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, GPIO0); // Use F4 style setup
		
		    // Configure EXTI0 interrupt for PA0
		    nvic_enable_irq(NVIC_EXTI0_IRQ);
		    exti_select_source(EXTI0, GPIOA); // Map EXTI0 line to GPIOA
		    // Trigger on the FALLING edge (button press connects PA0 to GND)
		    exti_set_trigger(EXTI0, EXTI_TRIGGER_FALLING);
		    exti_enable_request(EXTI0); // Enable interrupt request from EXTI0 line
		
		    // Reset state
		    last_tap_time = 0;
		    tap_interval = 0;
		    tap_detected_flag = false;
		    first_tap_registered = false; // Ensure we wait for the first proper tap
		}
		
		/**
		 * @brief Interrupt Service Routine for EXTI0 (PA0).
		 */
		void exti0_isr(void) {
		    // It's essential to clear the interrupt flag first
		    exti_reset_request(EXTI0);
		
		    uint32_t now = millis();
		
		    // Simple debounce check based on time since last valid tap
		    if (now - last_tap_time > DEBOUNCE_DELAY_MS) { // Use constant for debounce
		        if (!first_tap_registered) {
		            // This is the very first tap after init or reset
		            last_tap_time = now;
		            first_tap_registered = true;
		            // Do not calculate interval or set flag yet
		        } else {
		            // This is the second or subsequent tap
		            tap_interval = now - last_tap_time; // Calculate interval from the previous tap
		            last_tap_time = now;
		            tap_detected_flag = true; // Set flag for the main loop to process the interval
		        }
		    }
		    // Ignore taps that occur too close together (debounce)
		}
		
		/**
		 * @brief Checks if a tap interval was calculated since the last call.
		 * Note: This now signals that a valid *interval* is ready, not just a tap event.
		 * @return true if a new interval is available, false otherwise.
		 */
		bool tap_detected(void) {
		    if (tap_detected_flag) {
		        tap_detected_flag = false; // Clear the flag once checked
		        return true;
		    }
		    return false;
		}
		
		/**
		 * @brief Gets the last measured interval between taps.
		 * Should only be called after tap_detected() returns true.
		 * @return Interval in milliseconds.
		 */
		uint32_t tap_get_interval(void) {
		    // The interval is calculated in the ISR before the flag is set.
		    // No need for extra checks here, assuming it's called correctly.
		    return tap_interval;
		}

		/**
		 * @brief Returns the raw state of the tap button (PA0).
		 * Required by input_handler for mode switching logic.
		 * @return true if button is pressed (PA0 is LOW), false otherwise.
		 */
		bool tap_is_button_pressed(void) {
		    return (gpio_get(GPIOA, GPIO0) == 0); // Pulled up, pressed is LOW
		}

		/**
		 * @brief Checks if the time since the last tap exceeds the timeout.
		 * Resets the first_tap_registered flag if timed out.
		 * Should be called periodically (e.g., from SysTick).
		 * @param current_time_ms Current system time in milliseconds.
		 */
		void tap_check_timeout(uint32_t current_time_ms) {
		    if (first_tap_registered && (current_time_ms - last_tap_time > TAP_TIMEOUT_MS)) {
		        // Timeout occurred after the first tap was registered
		        first_tap_registered = false; // Reset tap sequence
		        tap_interval = 0;
		        // tap_detected_flag should already be false or will be ignored
		    }
		}
