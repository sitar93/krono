#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include <libopencm3/cm3/nvic.h>
#include <libopencm3/cm3/systick.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/flash.h>

#include "drivers/io.h" 
#include "drivers/persistence.h" 
#include "drivers/ext_clock.h" 
#include "drivers/tap.h"
#include "modes/modes.h" 
#include "input_handler.h"
#include "input_tempo.h"
#include "clock_manager.h"
#include "status_led.h"
#include "variables.h"
#include "main_constants.h"
#include "mode_state.h"
#include "krono_aux_led_pattern.h"

#include "modes/mode_fixed.h"

// --- Global State (Static to this file) ---
krono_state_t current_state; 
static volatile operational_mode_t g_current_op_mode = MODE_DEFAULT;
static volatile calculation_mode_t g_current_calc_mode = CALC_MODE_NORMAL;
static volatile bool state_changed_for_saving = false;
static uint32_t last_save_time = 0;

// --- Status LED (PA3) Blink Timer ---
static volatile uint32_t status_led_pa3_blink_end_time = 0; 
#define STATUS_LED_PA3_BLINK_DURATION_MS 100 

void krono_aux_led_cancel_soft_timer(void) {
    status_led_pa3_blink_end_time = 0;
}

static void pa3_soft_blink_arm(void) {
    krono_aux_led_pattern_cancel();
    set_output(JACK_OUT_AUX_LED_PA3, true);
    status_led_pa3_blink_end_time = millis() + STATUS_LED_PA3_BLINK_DURATION_MS;
}

// --- SysTick ---
volatile uint32_t system_millis = 0;

void sys_tick_handler(void) {
	system_millis++;
}

#ifndef MILLIS_DEFINED
#define MILLIS_DEFINED
uint32_t millis(void) {
	return system_millis;
}
#endif

// --- Helper Functions ---
static void save_current_state(void);

// Input Handler Callbacks
static void on_tap_tempo_change(uint32_t new_interval_ms, bool is_external_clock, uint32_t event_timestamp_ms,
                               bool tap_quadruple_boundary);
static void on_op_mode_change(uint8_t mode_clicks);
static void on_calc_mode_change(void);
static void on_fixed_bank_change(void);
static void on_save_request_from_input_handler(void); 
static void on_aux_led_blink_request_from_input_handler(void);
static void on_gamma_arm_aux_pattern_from_input_handler(void);
static void on_mod_press(mod_press_event_t event, uint32_t timestamp_ms);

// Forward Declarations
static void system_init(void);
static void configure_unused_pins(void);


// --- Input Handler Callback Implementations ---
static void on_tap_tempo_change(uint32_t new_interval_ms, bool is_external_clock, uint32_t event_timestamp_ms,
                                bool tap_quadruple_boundary) {
    if (new_interval_ms > 0) {
        if (tap_quadruple_boundary) {
            clock_manager_arm_tap_quadruple_boundary(new_interval_ms, event_timestamp_ms);
        } else {
            clock_manager_set_internal_tempo(new_interval_ms, is_external_clock, event_timestamp_ms);
        }
        pa3_soft_blink_arm();
    }
}

static void on_op_mode_change(uint8_t mode_clicks) {
    if (mode_clicks > 0 && mode_clicks <= NUM_OPERATIONAL_MODES) {
        operational_mode_t desired_mode = (operational_mode_t)(mode_clicks - 1);
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
        status_led_set_override(false, false); 
        clock_manager_set_calc_mode(g_current_calc_mode); 

        mode_state_apply_runtime(g_current_op_mode, &current_state);

        input_handler_update_main_op_mode(g_current_op_mode); 

        status_led_set_mode(g_current_op_mode); 
        status_led_reset(); 

        pa3_soft_blink_arm();
        
        state_changed_for_saving = true; 
    }
}

// on_calc_mode_change: call clock_manager_sync_flags(true) to reset mode timing (e.g. swing).
static void on_calc_mode_change(void) {
    g_current_calc_mode = (g_current_calc_mode == CALC_MODE_NORMAL) ? CALC_MODE_SWAPPED : CALC_MODE_NORMAL;
    io_cancel_all_timed_pulses();
    clock_manager_sync_flags(true); // This call resets the mode (e.g. swing indices)
    clock_manager_set_calc_mode(g_current_calc_mode); 

#if SAVE_CALC_MODE_PER_OP_MODE
     if (g_current_op_mode < NUM_OPERATIONAL_MODES) {
        current_state.calc_mode_per_op_mode[g_current_op_mode] = g_current_calc_mode;
     }
#endif

    pa3_soft_blink_arm();
}

static void on_fixed_bank_change(void) {
    // MOD press only cycles banks in FIXED mode
    if (g_current_op_mode == MODE_FIXED) {
        uint8_t current_bank = mode_fixed_get_bank();
        uint8_t next_bank = (current_bank + 1) % NUM_FIXED_BANKS;
        
        mode_fixed_set_bank(next_bank);
        current_state.fixed_bank = next_bank;
        
        pa3_soft_blink_arm();
    }
}

static void on_save_request_from_input_handler(void) {
    state_changed_for_saving = true;
}

static void on_aux_led_blink_request_from_input_handler(void) {
    pa3_soft_blink_arm();
}

static void on_gamma_arm_aux_pattern_from_input_handler(void) {
    krono_aux_led_pattern_start(2, AUX_LED_MULTI_PULSE_ON_MS, AUX_LED_MULTI_PULSE_GAP_MS);
}

static void on_mod_press(mod_press_event_t event, uint32_t timestamp_ms) {
    (void)event;
    mode_dispatch_mod_press(g_current_op_mode, MOD_PRESS_EVENT_SINGLE, timestamp_ms);
    pa3_soft_blink_arm();
}

// --- Pin Configuration ---
static void configure_unused_pins(void) {
    rcc_periph_clock_enable(RCC_GPIOA);
    rcc_periph_clock_enable(RCC_GPIOB);
    rcc_periph_clock_enable(RCC_GPIOC);

    uint32_t unused_pa_pins = GPIO4 | GPIO5 | GPIO6 | GPIO7 | GPIO8 |
                              GPIO9 | GPIO10 | GPIO11 | GPIO12 | GPIO13 | GPIO14;
    gpio_mode_setup(GPIOA, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, unused_pa_pins);

    uint32_t unused_pb_pins = GPIO2 | GPIO7;
    gpio_mode_setup(GPIOB, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, unused_pb_pins);

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
             current_state.op_mode = g_current_op_mode; 
             g_current_calc_mode = CALC_MODE_NORMAL;
             current_state.calc_mode_per_op_mode[g_current_op_mode] = g_current_calc_mode;
        }
#else
        g_current_calc_mode = CALC_MODE_NORMAL; 
#endif
        if (current_state.tempo_interval < MIN_INTERVAL || current_state.tempo_interval > MAX_INTERVAL) {
            current_state.tempo_interval = DEFAULT_TEMPO_INTERVAL;
        }
        mode_state_validate(&current_state);

        clock_manager_init(g_current_op_mode, current_state.tempo_interval);
        mode_state_apply_runtime(g_current_op_mode, &current_state);

        srand((unsigned int)millis() ^ 0xC001D00Du);

    } else {
        g_current_op_mode = current_state.op_mode; 
        g_current_calc_mode = CALC_MODE_NORMAL;
        current_state.tempo_interval = DEFAULT_TEMPO_INTERVAL;
#if SAVE_CALC_MODE_PER_OP_MODE
         if (g_current_op_mode < NUM_OPERATIONAL_MODES) { 
            g_current_calc_mode = current_state.calc_mode_per_op_mode[g_current_op_mode];
        }
#endif
        current_state.swing_profile_index_A = 3;
        current_state.swing_profile_index_B = 3;

        mode_state_validate(&current_state);
        clock_manager_init(g_current_op_mode, current_state.tempo_interval);
        mode_state_apply_runtime(g_current_op_mode, &current_state);

        srand((unsigned int)millis() ^ 0xC001D00Du);
    }

    input_handler_init(
        on_tap_tempo_change,
        on_op_mode_change,
        on_calc_mode_change,
        on_fixed_bank_change,
        on_save_request_from_input_handler,
        on_aux_led_blink_request_from_input_handler,
        on_gamma_arm_aux_pattern_from_input_handler,
        on_mod_press);
    input_handler_update_main_op_mode(g_current_op_mode); 

    clock_manager_set_calc_mode(g_current_calc_mode);
    status_led_init(); 
    status_led_set_mode(g_current_op_mode); 
}

// --- State Persistence ---

static void save_current_state(void) {
    io_all_outputs_off();
    io_cancel_all_timed_pulses();

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

    mode_state_capture_for_save(g_current_op_mode, &current_state, &state_to_save);
    state_to_save.fixed_sequence = 0; // Not used anymore

    state_to_save.checksum = 0; 
    state_to_save.checksum = persistence_calculate_checksum(&state_to_save);

    bool save_successful = persistence_save_state(&state_to_save);

    clock_manager_sync_flags(false); 
    clock_manager_set_calc_mode(g_current_calc_mode); 

    if (save_successful) {
        current_state = state_to_save; 
    }
    state_changed_for_saving = false; 
}


// --- Main Function ---

int main(void) {
    system_init();

    while (1) {
        uint32_t now = millis();
        if (tap_check_timeout(now)) {
            input_tempo_reset_calculation();
        }
        input_handler_update();
        clock_manager_update(); 
        status_led_update(now); 

        krono_aux_led_pattern_pump(now);

        if (!krono_aux_led_pattern_active()
            && status_led_pa3_blink_end_time != 0
            && now >= status_led_pa3_blink_end_time) {
            set_output(JACK_OUT_AUX_LED_PA3, false);
            status_led_pa3_blink_end_time = 0;
        }

        
        if (state_changed_for_saving) {
            if (now - last_save_time > SAVE_STATE_COOLDOWN_MS) {
                save_current_state(); 
                last_save_time = now;
            }
        }
    }

    return 0;
}
