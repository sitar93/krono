#ifndef KRONO_AUX_LED_PATTERN_H
#define KRONO_AUX_LED_PATTERN_H

#include <stdbool.h>
#include <stdint.h>

/**
 * Non-blocking multi-flash on PA3 with explicit OFF between pulses (unlike chained 100 ms soft blinks).
 * Call krono_aux_led_pattern_pump() every main loop iteration. While active, main must not apply the
 * simple PA3-off timeout (see krono_aux_led_pattern_active()).
 */
void krono_aux_led_pattern_start(uint8_t pulse_count, uint32_t on_ms, uint32_t gap_ms);
void krono_aux_led_pattern_cancel(void);
bool krono_aux_led_pattern_active(void);
void krono_aux_led_pattern_pump(uint32_t now_ms);

#endif
