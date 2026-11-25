#ifndef MODE_DELTA_SHARED_H
#define MODE_DELTA_SHARED_H

#include <stdbool.h>
#include <stdint.h>

#include "modes.h"

/** Internal helpers shared by mode_delta.c (31-35) and mode_delta_audio.c (36-40). */

#define DELTA_TWO_PI 6.28318531f

/** Persisted per-mode parameter bytes (index 0..9 = modes 31..40). */
extern uint8_t delta_param[MODE_DELTA_NUM_MODES];

/** Beat clock tracked from F1: whole beats + fraction of the current beat (tempo-synced phase). */
typedef struct {
    uint32_t beats;
    uint32_t last_edge_ms;
    bool started;
    bool half_fired;
} delta_beat_t;

void delta_beat_reset(delta_beat_t *b);
/** Advance on F1; returns position in beats (monotonic, fraction held below 1 until the next F1). */
float delta_beat_position(delta_beat_t *b, const mode_context_t *ctx);

/** True once per beat when the position passes the half beat (counter-phase clock). */
bool delta_beat_half_crossed(delta_beat_t *b, float pos);

void delta_all_gates_off(void);

uint32_t delta_rand(uint32_t *state);
float delta_rand01(uint32_t *state);

#endif
