
#include <stdbool.h>
#include <stdint.h>

#include <libopencm3/cm3/nvic.h>
#include <libopencm3/cm3/systick.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/flash.h>

#include "drivers/io.h" // Includes pin definitions and basic I/O control
#include "drivers/persistence.h" // Includes state saving/loading
#include "drivers/ext_clock.h" // External clock detection
#include "modes/modes.h" // Includes mode definitions and control
#include "input_handler.h" // Includes input detection and callbacks
#include "clock_manager.h" // Includes clock generation and mode dispatch
#include "status_led.h" // Includes status LED control (PA15)
#include "variables.h" // Includes tunable parameters
#include "main_constants.h" // Includes timing constants

// Specific mode headers for persistence get/set functions
#include "modes/mode_chaos.h" // Needed for get/set divisor

// --- Global State (Static to this file) ---
static krono_state_t current_state; // Holds loaded/saved state
static volatile operational_mode_t g_current_op_mode = MODE_DEFAULT;
static volatile calculation_mode_t g_current_calc_mode = CALC_MODE_NORMAL;
static volatile bool state_changed_for_saving = false;
static uint32_t last_save_time = 0;

// --- Status LED (PA3) Blink Timer ---
static volatile uint32_t status_led_pa3_blink_end_time = 0; // 0 means inactive (Renamed from aux_led_...)
#define STATUS_LED_PA3_BLINK_DURATION_MS 100 // Blink duration for PA3 status LED

// --- SysTick ---
volatile uint32_t system_millis = 0;

/**
 * @brief System Tick ISR - increments system_millis every millisecond.
 */
void sys_tick_handler(void) {
	system_millis++;
}

/**
 * @brief Provides a non-blocking delay in milliseconds (requires SysTick).
 * @return Current system time in milliseconds.
 */
#ifndef MILLIS_DEFINED // Prevent redefinition if included elsewhere
#define MILLIS_DEFINED
uint32_t millis(void) {
	return system_millis;
}
#endif

// --- Helper Functions ---

static void save_current_state(void);

// Input Handler Callbacks
static void on_tap_tempo_change(uint32_t new_interval_ms, bool is_external_clock, uint32_t event_timestamp_ms);
static void on_op_mode_change(uint8_t mode_clicks);
static void on_calc_mode_change(void);

// Forward Declarations
static void system_init(void);
static void configure_unused_pins(void);


// --- Input Handler Callback Implementations ---
static void on_tap_tempo_change(uint32_t new_interval_ms, bool is_external_clock, uint32_t event_timestamp_ms) {
    if (new_interval_ms > 0) {
        clock_manager_set_internal_tempo(new_interval_ms, is_external_clock, event_timestamp_ms);
        // Trigger Status LED (PA3) blink (manual software timer)
        set_output(JACK_OUT_AUX_LED_PA3, true);
        status_led_pa3_blink_end_time = millis() + STATUS_LED_PA3_BLINK_DURATION_MS;
    }
}

static void on_op_mode_change(uint8_t mode_clicks) {
	// mode_clicks represents the desired mode number (1-based)
	if (mode_clicks > 0 && mode_clicks <= NUM_OPERATIONAL_MODES) {
		operational_mode_t desired_mode = (operational_mode_t)(mode_clicks - 1);

		// bool mode_is_changing = (desired_mode != g_current_op_mode); // Unused variable

		g_current_op_mode = desired_mode;
#if SAVE_CALC_MODE_PER_OP_MODE
		if (g_current_op_mode < NUM_OPERATIONAL_MODES) {
			g_current_calc_mode = current_state.calc_mode_per_op_mode[g_current_op_mode];
		}
#endif
		io_cancel_all_timed_pulses();
		        io_all_outputs_off();
		clock_manager_sync_flags(false);
		clock_manager_set_operational_mode(g_current_op_mode);
		        status_led_set_override(false, false); // Reset PA15 LED override
		clock_manager_set_calc_mode(g_current_calc_mode);

		if (g_current_op_mode == MODE_CHAOS) {
			mode_chaos_set_divisor(current_state.chaos_mode_divisor);
		}

		status_led_set_mode(g_current_op_mode); // Update PA15 LED mode
		status_led_reset(); // Reset PA15 LED sequence

		// Trigger Status LED (PA3) blink (manual software timer)
		set_output(JACK_OUT_AUX_LED_PA3, true);
		status_led_pa3_blink_end_time = millis() + STATUS_LED_PA3_BLINK_DURATION_MS;

		state_changed_for_saving = true;
	}
}

static void on_calc_mode_change(void) {
	g_current_calc_mode = (g_current_calc_mode == CALC_MODE_NORMAL) ? CALC_MODE_SWAPPED : CALC_MODE_NORMAL;
	io_cancel_all_timed_pulses();
	clock_manager_sync_flags(true);
	clock_manager_set_calc_mode(g_current_calc_mode);
#if SAVE_CALC_MODE_PER_OP_MODE
	 if (g_current_op_mode < NUM_OPERATIONAL_MODES) {
		current_state.calc_mode_per_op_mode[g_current_op_mode] = g_current_calc_mode;
	 }
#endif

	// Trigger Status LED (PA3) blink (manual software timer)
	set_output(JACK_OUT_AUX_LED_PA3, true);
	status_led_pa3_blink_end_time = millis() + STATUS_LED_PA3_BLINK_DURATION_MS;
}

// --- Pin Configuration ---
static void configure_unused_pins(void) {
	rcc_periph_clock_enable(RCC_GPIOA);
	rcc_periph_clock_enable(RCC_GPIOB);
	rcc_periph_clock_enable(RCC_GPIOC);

	// PORT A - Used: PA0, PA1, PA2, PA3 (Status LED), PA15 (Mode LED). Unused: PA4-PA14
	uint32_t unused_pa_pins = GPIO4 | GPIO5 | GPIO6 | GPIO7 | GPIO8 |
	                          GPIO9 | GPIO10 | GPIO11 | GPIO12 | GPIO13 | GPIO14;
	gpio_mode_setup(GPIOA, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, unused_pa_pins);

	// PORT B - Used: PB0, PB1, PB3, PB4, PB5, PB6, PB8, PB9, PB10, PB12, PB13, PB14, PB15. Unused: PB2, PB7, PB11 (if exists)
	uint32_t unused_pb_pins = GPIO2 | GPIO7;
	gpio_mode_setup(GPIOB, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, unused_pb_pins);

	// PORT C - Assuming PC13, PC14, PC15 are unused
	uint32_t unused_pc_pins = GPIO13 | GPIO14 | GPIO15;
	gpio_mode_setup(GPIOC, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, unused_pc_pins);
}


// --- System Initialization ---

static void system_init(void) {
	rcc_clock_setup_pll(&rcc_hse_25mhz_3v3[RCC_CLOCK_3V3_84MHZ]);
	systick_set_clocksource(STK_CSR_CLKSOURCE_AHB_DIV8);
	systick_set_reload(10499);
	systick_interrupt_enable();
	systick_counter_enable();
	configure_unused_pins();
	io_init();
	pulse_timer_init();
	persistence_init();

	if (persistence_load_state(&current_state)) {
		g_current_op_mode = current_state.op_mode;
#if SAVE_CALC_MODE_PER_OP_MODE
		if (g_current_op_mode < NUM_OPERATIONAL_MODES) {
			 g_current_calc_mode = current_state.calc_mode_per_op_mode[g_current_op_mode];
		} else { 
			 g_current_op_mode = MODE_DEFAULT;
			 g_current_calc_mode = CALC_MODE_NORMAL;
			 state_changed_for_saving = true;
		}
#else
		g_current_calc_mode = CALC_MODE_NORMAL;
#endif
		if (current_state.tempo_interval < MIN_INTERVAL || current_state.tempo_interval > MAX_INTERVAL) {
			current_state.tempo_interval = DEFAULT_TEMPO_INTERVAL;
			state_changed_for_saving = true;
		}
		if (current_state.chaos_mode_divisor < CHAOS_DIVISOR_MIN ||
			current_state.chaos_mode_divisor > CHAOS_DIVISOR_DEFAULT ||
			(current_state.chaos_mode_divisor % CHAOS_DIVISOR_STEP != 0)) {
			 current_state.chaos_mode_divisor = CHAOS_DIVISOR_DEFAULT;
			 state_changed_for_saving = true;
		}

		clock_manager_init(g_current_op_mode, current_state.tempo_interval);

		if (g_current_op_mode == MODE_CHAOS) {
			 mode_chaos_set_divisor(current_state.chaos_mode_divisor);
		}

	} else {
		g_current_op_mode = MODE_DEFAULT;
		g_current_calc_mode = CALC_MODE_NORMAL;
#if SAVE_CALC_MODE_PER_OP_MODE
		for(int i=0; i < NUM_OPERATIONAL_MODES; ++i) { current_state.calc_mode_per_op_mode[i] = CALC_MODE_NORMAL; }
#endif
		current_state.chaos_mode_divisor = CHAOS_DIVISOR_DEFAULT;
		clock_manager_init(g_current_op_mode, DEFAULT_TEMPO_INTERVAL);
		save_current_state();
	}

	input_handler_init(
		on_tap_tempo_change,
		on_op_mode_change,
		on_calc_mode_change
	);

	clock_manager_set_calc_mode(g_current_calc_mode);
	status_led_init(); // Initializes PA15 LED control
	status_led_set_mode(g_current_op_mode); // Sets PA15 LED mode
}

// --- State Persistence ---

static void save_current_state(void) {
	krono_state_t state_to_save;
	state_to_save.magic_number = PERSISTENCE_MAGIC_NUMBER;
	state_to_save.tempo_interval = clock_manager_get_current_tempo_interval();
	state_to_save.op_mode = g_current_op_mode;
#if SAVE_CALC_MODE_PER_OP_MODE
	for(int i=0; i < NUM_OPERATIONAL_MODES; ++i) {
		state_to_save.calc_mode_per_op_mode[i] = current_state.calc_mode_per_op_mode[i];
	}
	if (g_current_op_mode < NUM_OPERATIONAL_MODES) { 
		state_to_save.calc_mode_per_op_mode[g_current_op_mode] = g_current_calc_mode;
	}
#endif
	if (g_current_op_mode == MODE_CHAOS) {
		 state_to_save.chaos_mode_divisor = mode_chaos_get_divisor();
	} else {
		 state_to_save.chaos_mode_divisor = current_state.chaos_mode_divisor;
	}

	state_to_save.checksum = persistence_calculate_checksum(&state_to_save);

	if(state_changed_for_saving) { 
		persistence_save_state(&state_to_save);
		current_state = state_to_save;
	}
}


// --- Main Function ---

int main(void) {
	system_init();

	while (1) {
		uint32_t now = millis();
		input_handler_update();
		clock_manager_update();
		status_led_update(now); // Updates PA15 Mode LED

		        // --- Manual Status LED (PA3) Blink Check ---
		        if (status_led_pa3_blink_end_time != 0 && now >= status_led_pa3_blink_end_time) {
		            set_output(JACK_OUT_AUX_LED_PA3, false); // Turn off Status LED (PA3)
		            status_led_pa3_blink_end_time = 0; // Mark timer as inactive
		        }

		// Debounced state saving
		if (state_changed_for_saving) {
			if (now - last_save_time > SAVE_STATE_COOLDOWN_MS) {
		                        save_current_state();
				state_changed_for_saving = false;
				last_save_time = now;
			}
		}
	}

	return 0;
}

