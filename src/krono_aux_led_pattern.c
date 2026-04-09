#include "krono_aux_led_pattern.h"

#include "drivers/io.h"
#include "main_constants.h"

void krono_aux_led_cancel_soft_timer(void);

typedef enum {
    PAT_IDLE = 0,
    PAT_ON,
    PAT_GAP,
} pat_phase_t;

static pat_phase_t s_phase = PAT_IDLE;
static uint8_t s_pulses_left;
static uint32_t s_on_ms;
static uint32_t s_gap_ms;
static uint32_t s_deadline;

void krono_aux_led_pattern_start(uint8_t pulse_count, uint32_t on_ms, uint32_t gap_ms) {
    if (pulse_count < 1u || on_ms < 1u) {
        return;
    }
    krono_aux_led_cancel_soft_timer();
    s_on_ms = on_ms;
    s_gap_ms = gap_ms;
    s_pulses_left = pulse_count;
    s_phase = PAT_ON;
    set_output(JACK_OUT_AUX_LED_PA3, true);
    s_deadline = millis() + on_ms;
}

void krono_aux_led_pattern_cancel(void) {
    if (s_phase == PAT_IDLE) {
        return;
    }
    s_phase = PAT_IDLE;
    set_output(JACK_OUT_AUX_LED_PA3, false);
}

bool krono_aux_led_pattern_active(void) {
    return s_phase != PAT_IDLE;
}

void krono_aux_led_pattern_pump(uint32_t now_ms) {
    switch (s_phase) {
    case PAT_IDLE:
        break;
    case PAT_ON:
        if ((int32_t)(now_ms - s_deadline) >= 0) {
            set_output(JACK_OUT_AUX_LED_PA3, false);
            s_pulses_left--;
            if (s_pulses_left == 0u) {
                s_phase = PAT_IDLE;
            } else {
                s_phase = PAT_GAP;
                s_deadline = now_ms + s_gap_ms;
            }
        }
        break;
    case PAT_GAP:
        if ((int32_t)(now_ms - s_deadline) >= 0) {
            set_output(JACK_OUT_AUX_LED_PA3, true);
            s_phase = PAT_ON;
            s_deadline = now_ms + s_on_ms;
        }
        break;
    default:
        s_phase = PAT_IDLE;
        break;
    }
}
