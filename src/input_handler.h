#ifndef INPUT_HANDLER_H
#define INPUT_HANDLER_H

#include <stdint.h>
#include <stdbool.h>
#include "modes/modes.h" // For operational_mode_t

// Callback types
typedef void (*input_tempo_change_callback_t)(uint32_t new_interval_ms, bool is_external, uint32_t event_time_ms);
typedef void (*input_op_mode_change_callback_t)(uint8_t mode_increment_clicks);
typedef void (*input_calc_mode_change_callback_t)(void);
typedef void (*input_save_request_callback_t)(void);
typedef void (*input_aux_led_blink_request_callback_t)(void); // New callback for Aux LED blink

void input_handler_init(
    input_tempo_change_callback_t tempo_cb_param,
    input_op_mode_change_callback_t op_mode_cb_param,
    input_calc_mode_change_callback_t calc_mode_cb_param,
    input_save_request_callback_t save_req_cb_param,
    input_aux_led_blink_request_callback_t aux_blink_cb_param // New parameter
);

void input_handler_update(void);
void input_handler_update_main_op_mode(operational_mode_t mode);

#endif // INPUT_HANDLER_H
