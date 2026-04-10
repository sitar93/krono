#ifndef INPUT_TEMPO_H
#define INPUT_TEMPO_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @param tap_quadruple_boundary true on clicks 4, 8, 12, …: F1 pulse at event_time_ms and tempo = mean of the
 *        three gaps in that quadruple; false is unused by tap tempo (kept for callback shape vs external path).
 */
typedef void (*input_tempo_emit_callback_t)(uint32_t new_interval_ms, bool is_external, uint32_t event_time_ms,
                                            bool tap_quadruple_boundary);

void input_tempo_init(input_tempo_emit_callback_t emit_cb);
void input_tempo_reset_calculation(void);
void input_tempo_handle_tap_event(void);
uint32_t input_tempo_get_last_reported_interval(void);
void input_tempo_set_last_reported_interval(uint32_t interval_ms);

#endif
