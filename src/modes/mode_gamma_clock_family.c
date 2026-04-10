#include "modes.h"
#include "../drivers/io.h"
#include "../main_constants.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

typedef enum {
    GCF_RATCHET = 0,
    GCF_ANTI_RATCHET,
    GCF_START_STOP
} gcf_variant_t;

static gcf_variant_t gcf_var;
static bool gcf_ratchet_double;
static bool gcf_anti_half;
static bool gcf_startstop_muted;

static const jack_output_t GCF_A[6] = {
    JACK_OUT_1A, JACK_OUT_2A, JACK_OUT_3A, JACK_OUT_4A, JACK_OUT_5A, JACK_OUT_6A
};
static const jack_output_t GCF_B[6] = {
    JACK_OUT_1B, JACK_OUT_2B, JACK_OUT_3B, JACK_OUT_4B, JACK_OUT_5B, JACK_OUT_6B
};
static const uint8_t GCF_F[6] = { 1u, 2u, 3u, 4u, 5u, 6u };

static uint32_t next_mult_trigger_time[NUM_JACK_OUTPUTS];
static uint32_t div_counters[NUM_JACK_OUTPUTS];
static bool waiting_for_first_f1;
/** Last raw tempo from context (unscaled); used on MOD to rescale mult deadlines. */
static uint32_t gcf_base_tempo_ms = DEFAULT_TEMPO_INTERVAL;

static uint32_t gcf_ratchet_Teff_for(uint32_t T, bool dbl) {
    if (T < MIN_INTERVAL) {
        T = MIN_INTERVAL;
    }
    if (T > MAX_INTERVAL) {
        T = MAX_INTERVAL;
    }
    if (gcf_var != GCF_RATCHET || !dbl) {
        return T;
    }
    uint32_t h = T / 2u;
    if (h < MIN_INTERVAL) {
        h = MIN_INTERVAL;
    }
    return h;
}

static uint32_t gcf_anti_Teff_for(uint32_t T, bool half) {
    if (T < MIN_INTERVAL) {
        T = MIN_INTERVAL;
    }
    if (T > MAX_INTERVAL) {
        T = MAX_INTERVAL;
    }
    if (gcf_var != GCF_ANTI_RATCHET || !half) {
        return T;
    }
    uint32_t d = T * 2u;
    if (d > MAX_INTERVAL) {
        d = MAX_INTERVAL;
    }
    return d;
}

static uint32_t gcf_effective_T(uint32_t T) {
    if (gcf_var == GCF_RATCHET) {
        return gcf_ratchet_Teff_for(T, gcf_ratchet_double);
    }
    if (gcf_var == GCF_ANTI_RATCHET) {
        return gcf_anti_Teff_for(T, gcf_anti_half);
    }
    return T;
}

static void gcf_scale_mult_triggers(uint32_t now, uint32_t old_teff, uint32_t new_teff) {
    if (old_teff == 0u || new_teff == 0u || old_teff == new_teff) {
        return;
    }
    for (int i = 0; i < 6; i++) {
        jack_output_t p = GCF_A[i];
        uint32_t n = next_mult_trigger_time[p];
        if (n > now) {
            uint64_t rem = (uint64_t)(n - now);
            rem = rem * (uint64_t)new_teff / (uint64_t)old_teff;
            if (rem > (uint64_t)MAX_INTERVAL * 4u) {
                rem = MAX_INTERVAL;
            }
            next_mult_trigger_time[p] = now + (uint32_t)rem;
        }
    }
}

static bool gcf_outputs_muted(void) {
    return (gcf_var == GCF_START_STOP) && gcf_startstop_muted;
}

static void gcf_outputs_all_low(void) {
    for (int i = 0; i < 6; i++) {
        set_output(GCF_A[i], false);
        set_output(GCF_B[i], false);
    }
}

static void gcf_resync_phase(void) {
    memset(div_counters, 0, sizeof(div_counters));
    uint32_t now = millis();
    for (int j = 0; j < (int)NUM_JACK_OUTPUTS; j++) {
        next_mult_trigger_time[j] = now;
    }
    gcf_outputs_all_low();
    waiting_for_first_f1 = true;
}

static void gcf_common_reset(void) {
    gcf_resync_phase();
}

void mode_gamma_ratchet_set_state(bool double_speed) {
    gcf_ratchet_double = double_speed;
}

void mode_gamma_ratchet_get_state(bool *double_speed) {
    if (double_speed != NULL) {
        *double_speed = gcf_ratchet_double;
    }
}

void mode_gamma_anti_ratchet_set_state(bool half_speed) {
    gcf_anti_half = half_speed;
}

void mode_gamma_anti_ratchet_get_state(bool *half_speed) {
    if (half_speed != NULL) {
        *half_speed = gcf_anti_half;
    }
}

void mode_gamma_start_stop_set_state(bool muted) {
    gcf_startstop_muted = muted;
}

void mode_gamma_start_stop_get_state(bool *muted) {
    if (muted != NULL) {
        *muted = gcf_startstop_muted;
    }
}

void mode_gamma_ratchet_init(void) {
    gcf_var = GCF_RATCHET;
    gcf_common_reset();
}

void mode_gamma_anti_ratchet_init(void) {
    gcf_var = GCF_ANTI_RATCHET;
    gcf_common_reset();
}

void mode_gamma_start_stop_init(void) {
    gcf_var = GCF_START_STOP;
    gcf_common_reset();
}

void mode_gamma_ratchet_reset(void) {
    gcf_var = GCF_RATCHET;
    gcf_common_reset();
}

void mode_gamma_anti_ratchet_reset(void) {
    gcf_var = GCF_ANTI_RATCHET;
    gcf_common_reset();
}

void mode_gamma_start_stop_reset(void) {
    gcf_var = GCF_START_STOP;
    gcf_common_reset();
}

static void gcf_shared_update(const mode_context_t *context) {
    uint32_t current_time = context->current_time_ms;
    uint32_t raw_T = context->current_tempo_interval_ms;
    if (raw_T >= MIN_INTERVAL && raw_T <= MAX_INTERVAL) {
        gcf_base_tempo_ms = raw_T;
    }
    uint32_t tempo_interval = gcf_effective_T(gcf_base_tempo_ms);
    bool tempo_valid = (tempo_interval >= MIN_INTERVAL && tempo_interval <= MAX_INTERVAL);
    const bool muted = gcf_outputs_muted();

    if (waiting_for_first_f1) {
        if (context->f1_rising_edge && tempo_valid) {
            waiting_for_first_f1 = false;
            uint32_t first_sync_time = current_time;
            memset(div_counters, 0, sizeof(div_counters));

            for (int i = 0; i < 6; i++) {
                uint32_t factor = GCF_F[i];
                if (factor == 0u) {
                    continue;
                }
                uint32_t mult_interval = tempo_interval / factor;
                if (mult_interval < MIN_CLOCK_INTERVAL) {
                    mult_interval = MIN_CLOCK_INTERVAL;
                }
                jack_output_t pin_a = GCF_A[i];
                jack_output_t pin_b = GCF_B[i];

                next_mult_trigger_time[pin_a] = first_sync_time + mult_interval;
                next_mult_trigger_time[pin_b] = first_sync_time + mult_interval;

                if (!muted && current_time >= next_mult_trigger_time[pin_a]) {
                    set_output_high_for_duration(pin_a, DEFAULT_PULSE_DURATION_MS);
                    next_mult_trigger_time[pin_a] += mult_interval;
                }
            }
            return;
        }
        if (!tempo_valid) {
            return;
        }
        return;
    }

    if (!tempo_valid) {
        return;
    }

    for (int i = 0; i < 6; i++) {
        uint32_t factor = GCF_F[i];
        if (factor == 0u) {
            continue;
        }
        jack_output_t pin_a = GCF_A[i];
        jack_output_t pin_b = GCF_B[i];

        uint32_t mult_interval = tempo_interval / factor;
        if (mult_interval < MIN_CLOCK_INTERVAL) {
            mult_interval = MIN_CLOCK_INTERVAL;
        }

        if (current_time >= next_mult_trigger_time[pin_a]) {
            if (!muted) {
                set_output_high_for_duration(pin_a, DEFAULT_PULSE_DURATION_MS);
            }
            next_mult_trigger_time[pin_a] += mult_interval;
            if (next_mult_trigger_time[pin_a] < current_time) {
                next_mult_trigger_time[pin_a] = current_time + mult_interval;
            }
        }

        if (context->f1_rising_edge) {
            div_counters[pin_b]++;
            if (div_counters[pin_b] >= factor) {
                if (!muted) {
                    set_output_high_for_duration(pin_b, DEFAULT_PULSE_DURATION_MS);
                }
                div_counters[pin_b] = 0;
            }
        }
    }
}

void mode_gamma_ratchet_update(const mode_context_t *context) {
    gcf_var = GCF_RATCHET;
    gcf_shared_update(context);
}

void mode_gamma_anti_ratchet_update(const mode_context_t *context) {
    gcf_var = GCF_ANTI_RATCHET;
    gcf_shared_update(context);
}

void mode_gamma_start_stop_update(const mode_context_t *context) {
    gcf_var = GCF_START_STOP;
    gcf_shared_update(context);
}

void mode_gamma_ratchet_on_mod_press(mod_press_event_t ev, uint32_t ts_ms) {
    (void)ts_ms;
    if (ev != MOD_PRESS_EVENT_SINGLE) {
        return;
    }
    gcf_var = GCF_RATCHET;
    uint32_t now = millis();
    uint32_t base = gcf_base_tempo_ms;
    if (base < MIN_INTERVAL || base > MAX_INTERVAL) {
        base = DEFAULT_TEMPO_INTERVAL;
    }
    bool was = gcf_ratchet_double;
    uint32_t old_e = gcf_ratchet_Teff_for(base, was);
    gcf_ratchet_double = !was;
    uint32_t new_e = gcf_ratchet_Teff_for(base, gcf_ratchet_double);
    gcf_scale_mult_triggers(now, old_e, new_e);
}

void mode_gamma_anti_ratchet_on_mod_press(mod_press_event_t ev, uint32_t ts_ms) {
    (void)ts_ms;
    if (ev != MOD_PRESS_EVENT_SINGLE) {
        return;
    }
    gcf_var = GCF_ANTI_RATCHET;
    uint32_t now = millis();
    uint32_t base = gcf_base_tempo_ms;
    if (base < MIN_INTERVAL || base > MAX_INTERVAL) {
        base = DEFAULT_TEMPO_INTERVAL;
    }
    bool was = gcf_anti_half;
    uint32_t old_e = gcf_anti_Teff_for(base, was);
    gcf_anti_half = !was;
    uint32_t new_e = gcf_anti_Teff_for(base, gcf_anti_half);
    gcf_scale_mult_triggers(now, old_e, new_e);
}

void mode_gamma_start_stop_on_mod_press(mod_press_event_t ev, uint32_t ts_ms) {
    (void)ts_ms;
    if (ev != MOD_PRESS_EVENT_SINGLE) {
        return;
    }
    gcf_var = GCF_START_STOP;
    gcf_startstop_muted = !gcf_startstop_muted;
}
