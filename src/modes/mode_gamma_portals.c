#include "modes.h"
#include "../drivers/io.h"
#include "../main_constants.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static const jack_output_t PORT_A[6] = {
    JACK_OUT_1A, JACK_OUT_2A, JACK_OUT_3A, JACK_OUT_4A, JACK_OUT_5A, JACK_OUT_6A
};
static const jack_output_t PORT_B[6] = {
    JACK_OUT_1B, JACK_OUT_2B, JACK_OUT_3B, JACK_OUT_4B, JACK_OUT_5B, JACK_OUT_6B
};

/**
 * Flash field `gamma_portals_div_on_a` stores **multiply** path (true) vs **divide** (false)
 * Default divide on F1 cadence; MOD → multiply (T/k toggle rate).
 */
static bool portals_multiply_mode;
/** true: A corridor “open” (held high), B closed (low). */
static bool portals_a_open[6];
static uint8_t portals_f1_count[6];
static uint32_t portals_last_swap_ms[6];

static void portals_apply_pair(uint8_t i) {
    bool a_on = portals_a_open[i];
    set_output(PORT_A[i], a_on);
    set_output(PORT_B[i], !a_on);
}

static void portals_toggle_pair(uint8_t i) {
    portals_a_open[i] = !portals_a_open[i];
    portals_apply_pair(i);
}

void mode_gamma_portals_set_state(bool multiply_mode) {
    portals_multiply_mode = multiply_mode;
}

void mode_gamma_portals_get_state(bool *multiply_mode) {
    if (multiply_mode != NULL) {
        *multiply_mode = portals_multiply_mode;
    }
}

void mode_gamma_portals_init(void) {
    mode_gamma_portals_reset();
}

void mode_gamma_portals_reset(void) {
    uint32_t now = millis();
    memset(portals_f1_count, 0, sizeof(portals_f1_count));
    for (uint8_t i = 0u; i < 6u; i++) {
        portals_a_open[i] = true;
        portals_last_swap_ms[i] = now;
        portals_apply_pair(i);
    }
}

void mode_gamma_portals_update(const mode_context_t *context) {
    uint32_t now = context->current_time_ms;
    uint32_t T = context->current_tempo_interval_ms;
    bool tempo_valid = (T >= MIN_INTERVAL && T <= MAX_INTERVAL);
    if (!tempo_valid) {
        return;
    }

    if (!portals_multiply_mode) {
        if (!context->f1_rising_edge) {
            return;
        }
        for (uint8_t i = 0u; i < 6u; i++) {
            uint32_t k = (uint32_t)i + 1u;
            portals_f1_count[i]++;
            if ((uint32_t)portals_f1_count[i] >= k) {
                portals_f1_count[i] = 0;
                portals_toggle_pair(i);
            }
        }
        return;
    }

    /* Multiply: pair k toggles every T/k ms (held levels). */
    for (uint8_t i = 0u; i < 6u; i++) {
        uint32_t k = (uint32_t)i + 1u;
        uint32_t per = T / k;
        if (per < MIN_CLOCK_INTERVAL) {
            per = MIN_CLOCK_INTERVAL;
        }
        uint8_t guard = 0u;
        while (now >= portals_last_swap_ms[i] + per && guard < 8u) {
            portals_toggle_pair(i);
            portals_last_swap_ms[i] += per;
            guard++;
        }
        if (guard >= 8u) {
            portals_last_swap_ms[i] = now;
        }
    }
}

void mode_gamma_portals_on_mod_press(mod_press_event_t ev, uint32_t ts_ms) {
    (void)ts_ms;
    if (ev != MOD_PRESS_EVENT_SINGLE) {
        return;
    }
    portals_multiply_mode = !portals_multiply_mode;
    uint32_t now = millis();
    memset(portals_f1_count, 0, sizeof(portals_f1_count));
    for (uint8_t i = 0u; i < 6u; i++) {
        portals_last_swap_ms[i] = now;
    }
}
