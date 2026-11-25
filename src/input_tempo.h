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

/** Reset tap quadruple pattern state only (does not clear last reported tempo). */
void input_tempo_reset_calculation(void);

/** Long tap inactivity: reset only if the tempo is not locked (a locked sequence can be resumed later). */
void input_tempo_on_tap_timeout(void);

/** Explicitly clear the last reported tap/restore interval. */
void input_tempo_clear_last_reported(void);

/** Result of processing a tap (for LED feedback). */
typedef enum {
    INPUT_TEMPO_EVT_NONE = 0, // No tap, or tap ignored as an outlier
    INPUT_TEMPO_EVT_COUNTED,  // Tap accepted (counting / refining)
    INPUT_TEMPO_EVT_LOCKED,   // Tempo locked (4th tap of a sequence): F1 fired on this press
} input_tempo_event_t;

input_tempo_event_t input_tempo_handle_tap_event(void);
uint32_t input_tempo_get_last_reported_interval(void);
void input_tempo_set_last_reported_interval(uint32_t interval_ms);

#endif
