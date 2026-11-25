#ifndef AUX_LED_H
#define AUX_LED_H

#include <stdbool.h>
#include <stdint.h>

/**
 * Aux LED (PA3, under MOD) renderer, driven by TIM2_CH4 hardware PWM.
 *
 * Layers, highest priority first:
 *  1. Event   - full-brightness flashes (tap, confirm, op-mode feedback). Driven through
 *               set_output(JACK_OUT_AUX_LED_PA3, ...), which forwards to aux_led_set_event().
 *  2. Count   - modes with enumerated state: N flashes repeated in a loop
 *               (long = tens, short = units, same encoding as the status LED).
 *  3. Status  - persistent mode state: steady on (freeze / bank B; event flashes become brief
 *               offs) or breathing while a modulation is active (peak follows its amount).
 */

typedef enum {
    AUX_LED_STATUS_NONE = 0, // Dark
    AUX_LED_STATUS_ON,       // Steady on: freeze / stop, or bank B selected. Input flashes invert (brief off)
    AUX_LED_STATUS_BREATHE,  // Modulation active; always breathing; `level` 0..1000 scales the peak
    AUX_LED_STATUS_COUNT,    // Enumerated value, `count` 1..10 flashed in a loop (2 s pause); MOD restarts it
} aux_led_status_kind_t;

typedef struct {
    aux_led_status_kind_t kind;
    uint16_t level; // AUX_LED_STATUS_BREATHE: 0..1000 peak scale
    uint8_t count;  // AUX_LED_STATUS_COUNT: 1..10
} aux_led_status_t;

/** Configure PA3 as TIM2_CH4 PWM output. Call after io_init(). */
void aux_led_init(void);

/** Event layer on/off (full brightness). Takes effect immediately, also before blocking flash saves. */
void aux_led_set_event(bool on);

/** Force full brightness regardless of status (mode-confirm feedback during the blocking save). */
void aux_led_force_on(bool on);

/** Request a count display of the current enumerated state (call after a MOD/CV action). */
void aux_led_request_count(void);

/** Cancel any running count display (e.g. on mode change). */
void aux_led_cancel_count(void);

/**
 * Render one frame. Call every main-loop iteration, after clock_manager_update() so mode
 * state already reflects the latest MOD action.
 * @param suppress_status true while op-mode select is active (status/count layers dark).
 */
void aux_led_update(uint32_t now_ms, const aux_led_status_t *status, bool suppress_status);

#endif
