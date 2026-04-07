#ifndef INPUT_TEMPO_H
#define INPUT_TEMPO_H

#include <stdbool.h>
#include <stdint.h>

typedef void (*input_tempo_emit_callback_t)(uint32_t new_interval_ms, bool is_external, uint32_t event_time_ms);

void input_tempo_init(input_tempo_emit_callback_t emit_cb);
void input_tempo_reset_calculation(void);
void input_tempo_handle_tap_event(void);
uint32_t input_tempo_get_last_reported_interval(void);
void input_tempo_set_last_reported_interval(uint32_t interval_ms);

#endif
