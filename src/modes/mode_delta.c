#include "modes.h"
#include "mode_delta_shared.h"
#include "../drivers/io.h"
#include "../drivers/analog_out.h"
#include "../main_constants.h"
#include "../variables.h"

#include <math.h>
#include <stdbool.h>

/*
 * DELTA tier, modulation modes 31-35: tempo-synced CV on the pseudo-DAC outputs 1A / 2A / 2B / 3A
 * (drivers/analog_out.c; 3A experimental); the other jacks are gates. Audio modes 36-40: mode_delta_audio.c.
 *
 * Common clocks for all DELTA modes: 1B = base clock (on the beat), 3B = counter-phase clock (half beat).
 * Automation gates: 4A 5A 6A 4B 5B 6B (per mode).
 * Persistence: one byte per mode in krono_state_t.delta_param[] (mode_delta_set_param / get_param).
 */

uint8_t delta_param[MODE_DELTA_NUM_MODES];

void mode_delta_set_param(uint8_t index, uint8_t value) {
    if (index < MODE_DELTA_NUM_MODES) {
        delta_param[index] = value;
    }
}

uint8_t mode_delta_get_param(uint8_t index) {
    return (index < MODE_DELTA_NUM_MODES) ? delta_param[index] : 0u;
}

// --- Shared helpers ---

#define CV_NUM 4u
static const analog_out_ch_t CV_OUT[CV_NUM] = {ANALOG_OUT_1A, ANALOG_OUT_2A, ANALOG_OUT_2B, ANALOG_OUT_3A};

void delta_beat_reset(delta_beat_t *b) {
    b->beats = 0;
    b->last_edge_ms = 0;
    b->started = false;
    b->half_fired = false;
}

float delta_beat_position(delta_beat_t *b, const mode_context_t *ctx) {
    if (ctx->f1_rising_edge) {
        if (b->started) {
            b->beats++;
        }
        b->started = true;
        b->last_edge_ms = ctx->current_time_ms;
        b->half_fired = false;
    }
    if (!b->started || ctx->current_tempo_interval_ms == 0u) {
        return 0.0f;
    }
    float frac = (float)(ctx->current_time_ms - b->last_edge_ms) / (float)ctx->current_tempo_interval_ms;
    if (frac > 0.999f) {
        frac = 0.999f; // Hold at the end of the beat until the next F1 (no overshoot when tempo slows)
    }
    return (float)b->beats + frac;
}

bool delta_beat_half_crossed(delta_beat_t *b, float pos) {
    if (b->started && !b->half_fired && (pos - (float)b->beats) >= 0.5f) {
        b->half_fired = true;
        return true;
    }
    return false;
}

void delta_all_gates_off(void) {
    for (jack_output_t j = JACK_OUT_1A; j <= JACK_OUT_6B; j = (jack_output_t)(j + 1)) {
        set_output(j, false);
    }
}

static void pulse(jack_output_t j) {
    set_output_high_for_duration(j, DEFAULT_PULSE_DURATION_MS);
}

/** 1B base clock on the beat, 3B counter-phase clock on the half beat. */
static void clock_gates(delta_beat_t *b, const mode_context_t *ctx, float pos) {
    if (ctx->f1_rising_edge) {
        pulse(JACK_OUT_1B);
    }
    if (delta_beat_half_crossed(b, pos)) {
        pulse(JACK_OUT_3B);
    }
}

/** Level-following gate output (only writes on change). */
static void gate_level(jack_output_t j, bool high, bool *state) {
    if (high != *state) {
        set_output(j, high);
        *state = high;
    }
}

static void cv_begin(void) {
    delta_all_gates_off();
    for (uint32_t i = 0; i < CV_NUM; i++) {
        analog_out_enable(CV_OUT[i], true);
        analog_out_set(CV_OUT[i], 0.0f);
    }
}

static void cv_end(void) {
    analog_out_disable_all();
    delta_all_gates_off();
}

/** Unipolar waveforms 0..1 for phase 0..1. */
typedef enum {
    DELTA_WAVE_SINE = 0,
    DELTA_WAVE_TRIANGLE,
    DELTA_WAVE_RAMP_UP,
    DELTA_WAVE_RAMP_DOWN,
    DELTA_WAVE_SQUARE,
    DELTA_NUM_WAVES
} delta_wave_t;

static float wave_value(delta_wave_t w, float phase) {
    switch (w) {
    case DELTA_WAVE_TRIANGLE:
        return (phase < 0.5f) ? (2.0f * phase) : (2.0f - 2.0f * phase);
    case DELTA_WAVE_RAMP_UP:
        return phase;
    case DELTA_WAVE_RAMP_DOWN:
        return 1.0f - phase;
    case DELTA_WAVE_SQUARE:
        return (phase < 0.5f) ? 1.0f : 0.0f;
    case DELTA_WAVE_SINE:
    default:
        return 0.5f - 0.5f * cosf(DELTA_TWO_PI * phase); // Starts at its minimum
    }
}

uint32_t delta_rand(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x ? x : 0x1234567u;
    return *state;
}

float delta_rand01(uint32_t *state) {
    return (float)(delta_rand(state) >> 8) / 16777216.0f;
}

static uint32_t lfo_seed = 0x3C6EF372u;

static float lfo_rnd(void) {
    return delta_rand01(&lfo_seed);
}

// =====================================================================================================
// 31 TIDES - tempo-synced LFOs
//   CV: 1A = 1 beat, 2A = 4 beats (1 bar), 2B = 16 beats (4 bars), 3A = 2 beats.
//   Gates: 4A / 5A / 6A = cycle start of 2A / 2B / 3A; 4B / 5B / 6B = cycle midpoint (peak) of 2A / 2B / 3A.
//   (1A start / midpoint = the 1B / 3B clocks.)
//   Evolution: the waveform crossfades to the next shape over 16 bars (sine -> triangle -> ramps -> square ->
//   ...); each bar there is a 10 % chance that 1A or 3A runs at double speed for that bar.
//   MOD: starting waveform (sine, triangle, ramp up, ramp down, square). Persisted.
// =====================================================================================================

static const uint32_t TIDES_BEATS[CV_NUM] = {1u, 4u, 16u, 2u};
static const jack_output_t TIDES_START[CV_NUM] = {JACK_OUT_1B, JACK_OUT_4A, JACK_OUT_5A, JACK_OUT_6A};
static const jack_output_t TIDES_MID[CV_NUM] = {JACK_OUT_3B, JACK_OUT_4B, JACK_OUT_5B, JACK_OUT_6B};
static delta_beat_t tides_beat;
static bool tides_mid_fired[CV_NUM];
static int32_t tides_last_bar;
static int32_t tides_double_out; // -1 = none, else CV index running x2 this bar

#define TIDES_MORPH_BEATS 64.0f // 16 bars per shape

void mode_delta_tides_init(void) {
    cv_begin();
    delta_beat_reset(&tides_beat);
    for (uint32_t i = 0; i < CV_NUM; i++) {
        tides_mid_fired[i] = false;
    }
    tides_last_bar = -1;
    tides_double_out = -1;
    if (delta_param[0] >= DELTA_NUM_WAVES) {
        delta_param[0] = 0;
    }
}

void mode_delta_tides_reset(void) {
    cv_end();
}

void mode_delta_tides_update(const mode_context_t *ctx) {
    float const pos = delta_beat_position(&tides_beat, ctx);
    clock_gates(&tides_beat, ctx, pos);

    // Shape crossfade: wave k -> k+1 over TIDES_MORPH_BEATS, starting from the MOD-selected shape
    uint32_t const seg = (uint32_t)(pos / TIDES_MORPH_BEATS);
    float const xf = fmodf(pos, TIDES_MORPH_BEATS) / TIDES_MORPH_BEATS;
    delta_wave_t const w0 = (delta_wave_t)((delta_param[0] + seg) % DELTA_NUM_WAVES);
    delta_wave_t const w1 = (delta_wave_t)((delta_param[0] + seg + 1u) % DELTA_NUM_WAVES);

    int32_t const bar = tides_beat.started ? (int32_t)(tides_beat.beats / 4u) : -1;
    if (bar != tides_last_bar) {
        tides_last_bar = bar;
        tides_double_out = (lfo_rnd() < 0.1f) ? ((lfo_rnd() < 0.5f) ? 0 : 3) : -1;
    }

    for (uint32_t i = 0; i < CV_NUM; i++) {
        float len = (float)TIDES_BEATS[i];
        if ((int32_t)i == tides_double_out) {
            len *= 0.5f; // 1 or 2 beats -> still aligned at bar boundaries
        }
        float const phase = fmodf(pos, len) / len;
        float const a = wave_value(w0, phase);
        float const b = wave_value(w1, phase);
        analog_out_set(CV_OUT[i], a + xf * (b - a));
        if (i == 0u) {
            continue;
        }
        if (ctx->f1_rising_edge && (tides_beat.beats % TIDES_BEATS[i]) == 0u) {
            pulse(TIDES_START[i]);
            tides_mid_fired[i] = false;
        }
        if (!tides_mid_fired[i] && phase >= 0.5f) {
            pulse(TIDES_MID[i]);
            tides_mid_fired[i] = true;
        }
    }
}

void mode_delta_tides_on_mod_press(mod_press_event_t ev, uint32_t ts_ms) {
    (void)ev;
    (void)ts_ms;
    delta_param[0] = (uint8_t)((delta_param[0] + 1u) % DELTA_NUM_WAVES);
}

// =====================================================================================================
// 32 ORBIT - one sine LFO read at four phases 90 deg apart (1A, 2A, 2B, 3A): the peak circles the outputs.
//   Cycle length (beats): 2, 4, 8, 16 - MOD selects (persisted).
//   Evolution: the rotation reverses every 2-8 bars (random); the speed swings +-30 % over 16 bars.
//   Gates: 4A / 5A / 6A / 4B = peak of 1A / 2A / 2B / 3A; 5B = gate while 1A > 2B; 6B = full turn.
// =====================================================================================================

#define ORBIT_NUM_SPEEDS 4u
static const uint32_t ORBIT_BEATS[ORBIT_NUM_SPEEDS] = {2u, 4u, 8u, 16u};
static const float ORBIT_OFFSET[CV_NUM] = {0.0f, 0.25f, 0.5f, 0.75f};
static const jack_output_t ORBIT_PEAK[CV_NUM] = {JACK_OUT_4A, JACK_OUT_5A, JACK_OUT_6A, JACK_OUT_4B};
static delta_beat_t orbit_beat;
static bool orbit_upper[CV_NUM];
static bool orbit_cmp;
static float orbit_last_phase;
static float orbit_phase; // Accumulated rotation 0..1
static float orbit_last_pos;
static float orbit_dir;
static uint32_t orbit_next_flip_bar;
static int32_t orbit_last_bar;

void mode_delta_orbit_init(void) {
    cv_begin();
    delta_beat_reset(&orbit_beat);
    for (uint32_t i = 0; i < CV_NUM; i++) {
        orbit_upper[i] = false;
    }
    orbit_cmp = false;
    orbit_last_phase = 0.0f;
    orbit_phase = 0.0f;
    orbit_last_pos = 0.0f;
    orbit_dir = 1.0f;
    orbit_next_flip_bar = 2u + (uint32_t)(lfo_rnd() * 7.0f);
    orbit_last_bar = -1;
    if (delta_param[1] >= ORBIT_NUM_SPEEDS) {
        delta_param[1] = 0u;
    }
}

void mode_delta_orbit_reset(void) {
    cv_end();
}

void mode_delta_orbit_update(const mode_context_t *ctx) {
    float const pos = delta_beat_position(&orbit_beat, ctx);
    clock_gates(&orbit_beat, ctx, pos);
    uint8_t const speed = (delta_param[1] < ORBIT_NUM_SPEEDS) ? delta_param[1] : 0u;
    float const len = (float)ORBIT_BEATS[speed];

    int32_t const bar = orbit_beat.started ? (int32_t)(orbit_beat.beats / 4u) : -1;
    if (bar != orbit_last_bar) {
        orbit_last_bar = bar;
        if (bar >= 0 && (uint32_t)bar >= orbit_next_flip_bar) {
            orbit_dir = -orbit_dir;
            orbit_next_flip_bar = (uint32_t)bar + 2u + (uint32_t)(lfo_rnd() * 7.0f);
        }
    }
    float dpos = pos - orbit_last_pos;
    orbit_last_pos = pos;
    if (dpos < 0.0f || dpos > 1.0f) {
        dpos = 0.0f;
    }
    float const swing = 1.0f + 0.3f * sinf(DELTA_TWO_PI * pos / 64.0f);
    orbit_phase = fmodf(orbit_phase + orbit_dir * swing * dpos / len + 1.0f, 1.0f);
    float const base_phase = orbit_phase;

    float v[CV_NUM];
    for (uint32_t i = 0; i < CV_NUM; i++) {
        float const phase = fmodf(base_phase + ORBIT_OFFSET[i], 1.0f);
        v[i] = wave_value(DELTA_WAVE_SINE, phase);
        analog_out_set(CV_OUT[i], v[i]);
        bool const upper = phase >= 0.5f; // Peak reached at phase 0.5
        if (upper && !orbit_upper[i] && orbit_beat.started) {
            pulse(ORBIT_PEAK[i]);
        }
        orbit_upper[i] = upper;
    }
    gate_level(JACK_OUT_5B, v[0] > v[2], &orbit_cmp);
    if (orbit_beat.started && fabsf(base_phase - orbit_last_phase) > 0.5f) {
        pulse(JACK_OUT_6B); // Wrapped (either direction): one full turn
    }
    orbit_last_phase = base_phase;
}

void mode_delta_orbit_on_mod_press(mod_press_event_t ev, uint32_t ts_ms) {
    (void)ev;
    (void)ts_ms;
    delta_param[1] = (uint8_t)((delta_param[1] + 1u) % ORBIT_NUM_SPEEDS);
}

// =====================================================================================================
// 33 CHIMERA - morphing wavetables
//   8 single-cycle tables; a bank strings 4 of them. A slow triangle (32 beats) sweeps the morph position
//   through the bank (table 1 -> 4 -> 1).
//   CV: 1A = 1 beat cycle, 2A = 4 beats (morph offset half a bank), 2B = 1 beat with mirrored morph,
//       3A = the morph position itself.
//   Gates: 4A / 5A / 6A = morph enters a new table on 1A / 2A / 2B; 4B = 2A cycle start;
//          5B = gate while the morph sweep rises; 6B = morph sweep start.
//   Evolution: every 8 bars the bank jumps to a random other bank.
//   MOD: home bank (4, persisted) - selected immediately.
// =====================================================================================================

#define CHIMERA_TABLE_LEN 32u
#define CHIMERA_NUM_TABLES 8u
#define CHIMERA_NUM_BANKS 4u
#define CHIMERA_MORPH_BEATS 32.0f

static float chimera_tables[CHIMERA_NUM_TABLES][CHIMERA_TABLE_LEN + 1u]; // +1: wrap point
static bool chimera_built = false;
static const uint8_t CHIMERA_BANKS[CHIMERA_NUM_BANKS][4] = {
    {0u, 1u, 2u, 6u}, // sine -> triangle -> saw -> harmonic
    {3u, 5u, 4u, 7u}, // camel -> spike -> stairs -> wobble
    {0u, 3u, 5u, 6u}, // sine -> camel -> spike -> harmonic
    {7u, 4u, 2u, 5u}, // wobble -> stairs -> saw -> spike
};
static const jack_output_t CHIMERA_SEG_GATE[3] = {JACK_OUT_4A, JACK_OUT_5A, JACK_OUT_6A};
static delta_beat_t chimera_beat;
static int32_t chimera_last_seg[3];
static bool chimera_rising_state;
static float chimera_last_mp;
static uint8_t chimera_bank;
static int32_t chimera_last_bar;

static void chimera_build(void) {
    for (uint32_t i = 0; i <= CHIMERA_TABLE_LEN; i++) {
        float const x = (float)(i % CHIMERA_TABLE_LEN) / (float)CHIMERA_TABLE_LEN;
        float const s1 = sinf(DELTA_TWO_PI * x);
        chimera_tables[0][i] = 0.5f - 0.5f * cosf(DELTA_TWO_PI * x);                       // sine
        chimera_tables[1][i] = (x < 0.5f) ? 2.0f * x : 2.0f - 2.0f * x;                    // triangle
        chimera_tables[2][i] = x;                                                         // saw
        chimera_tables[3][i] = 0.5f - 0.5f * cosf(2.0f * DELTA_TWO_PI * x) * (1.0f - x);   // camel
        chimera_tables[4][i] = floorf(x * 4.0f) / 3.0f;                                    // stairs
        chimera_tables[5][i] = expf(-10.0f * fabsf(x - 0.5f));                             // spike
        chimera_tables[6][i] = 0.5f + 0.33f * (s1 + 0.5f * sinf(3.0f * DELTA_TWO_PI * x)); // harmonic
        chimera_tables[7][i] = 0.5f + 0.45f * sinf(DELTA_TWO_PI * x + 1.5f * sinf(3.0f * DELTA_TWO_PI * x));
    }
    for (uint32_t t = 0; t < CHIMERA_NUM_TABLES; t++) {
        for (uint32_t i = 0; i <= CHIMERA_TABLE_LEN; i++) {
            float const v = chimera_tables[t][i];
            chimera_tables[t][i] = (v < 0.0f) ? 0.0f : ((v > 1.0f) ? 1.0f : v);
        }
    }
    chimera_built = true;
}

static float chimera_read(uint8_t table, float phase) {
    float const x = phase * (float)CHIMERA_TABLE_LEN;
    uint32_t i = (uint32_t)x;
    if (i >= CHIMERA_TABLE_LEN) {
        i = CHIMERA_TABLE_LEN - 1u;
    }
    float const f = x - (float)i;
    return chimera_tables[table][i] + f * (chimera_tables[table][i + 1u] - chimera_tables[table][i]);
}

/** morph 0..3 across the 4 tables of the bank. */
static float chimera_value(uint8_t bank, float morph, float phase) {
    morph = (morph < 0.0f) ? 0.0f : ((morph > 3.0f) ? 3.0f : morph);
    uint32_t seg = (uint32_t)morph;
    if (seg > 2u) {
        seg = 2u;
    }
    float const f = morph - (float)seg;
    float const a = chimera_read(CHIMERA_BANKS[bank][seg], phase);
    float const b = chimera_read(CHIMERA_BANKS[bank][seg + 1u], phase);
    return a + f * (b - a);
}

void mode_delta_chimera_init(void) {
    if (!chimera_built) {
        chimera_build();
    }
    cv_begin();
    delta_beat_reset(&chimera_beat);
    for (uint32_t i = 0; i < 3u; i++) {
        chimera_last_seg[i] = -1;
    }
    chimera_rising_state = false;
    chimera_last_mp = 0.0f;
    chimera_last_bar = -1;
    if (delta_param[2] >= CHIMERA_NUM_BANKS) {
        delta_param[2] = 0u;
    }
    chimera_bank = delta_param[2];
}

void mode_delta_chimera_reset(void) {
    cv_end();
}

void mode_delta_chimera_update(const mode_context_t *ctx) {
    float const pos = delta_beat_position(&chimera_beat, ctx);
    clock_gates(&chimera_beat, ctx, pos);
    int32_t const bar = chimera_beat.started ? (int32_t)(chimera_beat.beats / 4u) : -1;
    if (bar != chimera_last_bar) {
        chimera_last_bar = bar;
        if (bar > 0 && (bar % 8) == 0) {
            chimera_bank = (uint8_t)((chimera_bank + 1u + (uint32_t)(lfo_rnd() * 3.0f)) % CHIMERA_NUM_BANKS);
        }
    }
    uint8_t const bank = (chimera_bank < CHIMERA_NUM_BANKS) ? chimera_bank : 0u;

    float const mp = fmodf(pos, CHIMERA_MORPH_BEATS) / CHIMERA_MORPH_BEATS;
    float const m = (mp < 0.5f) ? (6.0f * mp) : (6.0f - 6.0f * mp);
    float const morph[3] = {m, fmodf(m + 1.5f, 3.0f), 3.0f - m};
    float const cycle_beats[3] = {1.0f, 4.0f, 1.0f};

    for (uint32_t i = 0; i < 3u; i++) {
        float const phase = fmodf(pos, cycle_beats[i]) / cycle_beats[i];
        analog_out_set(CV_OUT[i], chimera_value(bank, morph[i], phase));
        int32_t const seg = (int32_t)morph[i];
        if (chimera_last_seg[i] >= 0 && seg != chimera_last_seg[i]) {
            pulse(CHIMERA_SEG_GATE[i]);
        }
        chimera_last_seg[i] = seg;
    }
    analog_out_set(ANALOG_OUT_3A, m / 3.0f);

    if (ctx->f1_rising_edge && (chimera_beat.beats % 4u) == 0u) {
        pulse(JACK_OUT_4B);
    }
    gate_level(JACK_OUT_5B, chimera_beat.started && mp < 0.5f, &chimera_rising_state);
    if (chimera_beat.started && mp < chimera_last_mp) {
        pulse(JACK_OUT_6B);
    }
    chimera_last_mp = mp;
}

void mode_delta_chimera_on_mod_press(mod_press_event_t ev, uint32_t ts_ms) {
    (void)ev;
    (void)ts_ms;
    delta_param[2] = (uint8_t)((delta_param[2] + 1u) % CHIMERA_NUM_BANKS);
    chimera_bank = delta_param[2];
}

// =====================================================================================================
// 34 WANDER - random voltages
//   New random target on 1A every beat, 2A every 2 beats, 2B every 4 beats, 3A every half beat.
//   MOD (persisted): 0 = smooth glide (cosine), 1 = stepped (sample & hold), 2 = slewed (glide over the first
//   quarter of the interval).
//   Gates: 4A / 5A / 6A = trigger on each new 2A / 2B / 3A value; 4B / 5B / 6B = high while 1A / 2A / 2B
//   is above half scale.
//   Evolution: every 4 bars each output gets a new range (width and centre) and "temperature" (how often a
//   new value is actually drawn instead of holding the previous one).
// =====================================================================================================

#define WANDER_NUM_STYLES 3u
static const float WANDER_BEATS[CV_NUM] = {1.0f, 2.0f, 4.0f, 0.5f};
static const jack_output_t WANDER_NEW_GATE[CV_NUM] = {JACK_OUT_1B, JACK_OUT_4A, JACK_OUT_5A, JACK_OUT_6A};
static const jack_output_t WANDER_CMP_GATE[3] = {JACK_OUT_4B, JACK_OUT_5B, JACK_OUT_6B};
static delta_beat_t wander_beat;
static float wander_from[CV_NUM];
static float wander_to[CV_NUM];
static int32_t wander_slot[CV_NUM];
static uint32_t wander_seed = 0x9E3779B9u;
static bool wander_gate_state[3];
static float wander_width[CV_NUM];
static float wander_center[CV_NUM];
static float wander_temp[CV_NUM];
static int32_t wander_last_bar;

void mode_delta_wander_init(void) {
    cv_begin();
    delta_beat_reset(&wander_beat);
    for (uint32_t i = 0; i < CV_NUM; i++) {
        wander_from[i] = 0.5f;
        wander_to[i] = 0.5f;
        wander_slot[i] = -1;
    }
    for (uint32_t i = 0; i < 3u; i++) {
        wander_gate_state[i] = false;
    }
    for (uint32_t i = 0; i < CV_NUM; i++) {
        wander_width[i] = 1.0f;
        wander_center[i] = 0.5f;
        wander_temp[i] = 1.0f;
    }
    wander_last_bar = -1;
    if (delta_param[3] >= WANDER_NUM_STYLES) {
        delta_param[3] = 0u;
    }
}

void mode_delta_wander_reset(void) {
    cv_end();
}

void mode_delta_wander_update(const mode_context_t *ctx) {
    float const pos = delta_beat_position(&wander_beat, ctx);
    clock_gates(&wander_beat, ctx, pos);
    wander_seed ^= ctx->current_time_ms;
    uint8_t const style = (delta_param[3] < WANDER_NUM_STYLES) ? delta_param[3] : 0u;

    int32_t const bar = wander_beat.started ? (int32_t)(wander_beat.beats / 4u) : -1;
    if (bar != wander_last_bar) {
        wander_last_bar = bar;
        if (bar >= 0 && (bar % 4) == 0) {
            for (uint32_t i = 0; i < CV_NUM; i++) {
                wander_width[i] = 0.2f + 0.8f * delta_rand01(&wander_seed);
                wander_center[i] = 0.5f * wander_width[i] + (1.0f - wander_width[i]) * delta_rand01(&wander_seed);
                wander_temp[i] = 0.3f + 0.7f * delta_rand01(&wander_seed);
            }
        }
    }

    for (uint32_t i = 0; i < CV_NUM; i++) {
        float const len = WANDER_BEATS[i];
        if (wander_beat.started) {
            int32_t const slot = (int32_t)(pos / len);
            if (slot != wander_slot[i]) {
                wander_slot[i] = slot;
                wander_from[i] = wander_to[i];
                if (delta_rand01(&wander_seed) < wander_temp[i]) {
                    float const r = delta_rand01(&wander_seed) - 0.5f;
                    float const v = wander_center[i] + r * wander_width[i];
                    wander_to[i] = (v < 0.0f) ? 0.0f : ((v > 1.0f) ? 1.0f : v);
                }
                if (i > 0u) {
                    pulse(WANDER_NEW_GATE[i]);
                }
            }
        }
        float const t = fmodf(pos, len) / len;
        float v;
        if (style == 1u) {
            v = wander_to[i];
        } else if (style == 2u) {
            float const k = (t < 0.25f) ? (t * 4.0f) : 1.0f;
            v = wander_from[i] + k * (wander_to[i] - wander_from[i]);
        } else {
            float const k = 0.5f - 0.5f * cosf(3.14159265f * t);
            v = wander_from[i] + k * (wander_to[i] - wander_from[i]);
        }
        analog_out_set(CV_OUT[i], v);
        if (i < 3u) {
            gate_level(WANDER_CMP_GATE[i], v > 0.5f, &wander_gate_state[i]);
        }
    }
}

void mode_delta_wander_on_mod_press(mod_press_event_t ev, uint32_t ts_ms) {
    (void)ev;
    (void)ts_ms;
    delta_param[3] = (uint8_t)((delta_param[3] + 1u) % WANDER_NUM_STYLES);
}

// =====================================================================================================
// 35 BUTTERFLY - Lorenz chaotic attractor (sigma 10, rho 28, beta 8/3)
//   CV: 1A = x, 2A = y, 2B = z, 3A = speed along the trajectory. Speed follows the tempo;
//   MOD selects x0.25 / x0.5 / x1 / x2 (persisted).
//   Gates: 4A = x > 0, 5A = y > 0, 6A = z above its midline; 4B = lobe switch (x sign change),
//          5B = each z peak, 6B = y sign change.
//   Evolution: rho drifts between ~20 and ~44 over 16 bars (plus a slow random walk): below ~24.7 the
//   trajectory settles into calm spirals, above it turns fully chaotic.
// =====================================================================================================

#define LORENZ_NUM_SPEEDS 4u
static const float LORENZ_SPEED[LORENZ_NUM_SPEEDS] = {0.25f, 0.5f, 1.0f, 2.0f};
static const jack_output_t LORENZ_SIGN_GATE[3] = {JACK_OUT_4A, JACK_OUT_5A, JACK_OUT_6A};
static delta_beat_t lorenz_beat;
static float lz_x, lz_y, lz_z, lz_dz_prev;
static uint32_t lorenz_last_ms;
static bool lz_gate[3];
static float lz_rho_walk;

void mode_delta_butterfly_init(void) {
    cv_begin();
    delta_beat_reset(&lorenz_beat);
    lz_x = 0.1f;
    lz_y = 0.0f;
    lz_z = 20.0f;
    lz_dz_prev = 0.0f;
    lz_rho_walk = 0.0f;
    lorenz_last_ms = 0;
    for (uint32_t i = 0; i < 3u; i++) {
        lz_gate[i] = false;
    }
    if (delta_param[4] >= LORENZ_NUM_SPEEDS) {
        delta_param[4] = 2u;
    }
}

void mode_delta_butterfly_reset(void) {
    cv_end();
}

void mode_delta_butterfly_update(const mode_context_t *ctx) {
    float const pos = delta_beat_position(&lorenz_beat, ctx);
    clock_gates(&lorenz_beat, ctx, pos);
    uint32_t const now = ctx->current_time_ms;
    if (lorenz_last_ms == 0u) {
        lorenz_last_ms = now;
        return;
    }
    uint32_t elapsed = now - lorenz_last_ms;
    if (elapsed == 0u) {
        return;
    }
    if (elapsed > 50u) {
        elapsed = 50u;
    }
    lorenz_last_ms = now;

    // ~1 attractor time unit per 2 beats at x1
    uint8_t const sp = (delta_param[4] < LORENZ_NUM_SPEEDS) ? delta_param[4] : 2u;
    float const interval = (ctx->current_tempo_interval_ms > 0u) ? (float)ctx->current_tempo_interval_ms : 500.0f;
    float const dt_total = LORENZ_SPEED[sp] * (float)elapsed / (2.0f * interval);
    uint32_t const steps = (uint32_t)(dt_total / 0.004f) + 1u;
    float const dt = dt_total / (float)steps;
    float const x_prev = lz_x;
    float const y_prev = lz_y;
    float dx = 0.0f, dy = 0.0f, dz = 0.0f;
    lz_rho_walk += (lfo_rnd() - 0.5f) * 0.02f;
    lz_rho_walk = (lz_rho_walk < -4.0f) ? -4.0f : ((lz_rho_walk > 4.0f) ? 4.0f : lz_rho_walk);
    float const rho = 32.0f + 10.0f * sinf(DELTA_TWO_PI * pos / 64.0f) + lz_rho_walk;
    for (uint32_t s = 0; s < steps; s++) {
        dx = 10.0f * (lz_y - lz_x);
        dy = lz_x * (rho - lz_z) - lz_y;
        dz = lz_x * lz_y - (8.0f / 3.0f) * lz_z;
        lz_x += dx * dt;
        lz_y += dy * dt;
        lz_z += dz * dt;
    }

    analog_out_set(ANALOG_OUT_1A, lz_x / 40.0f + 0.5f);
    analog_out_set(ANALOG_OUT_2A, lz_y / 56.0f + 0.5f);
    analog_out_set(ANALOG_OUT_2B, lz_z / (1.8f * rho));
    analog_out_set(ANALOG_OUT_3A, sqrtf(dx * dx + dy * dy + dz * dz) / 250.0f);

    bool const g[3] = {lz_x > 0.0f, lz_y > 0.0f, lz_z > rho - 1.0f};
    for (uint32_t i = 0; i < 3u; i++) {
        gate_level(LORENZ_SIGN_GATE[i], g[i], &lz_gate[i]);
    }
    if ((x_prev > 0.0f) != (lz_x > 0.0f)) {
        pulse(JACK_OUT_4B);
    }
    if (lz_dz_prev > 0.0f && dz <= 0.0f) {
        pulse(JACK_OUT_5B);
    }
    if ((y_prev > 0.0f) != (lz_y > 0.0f)) {
        pulse(JACK_OUT_6B);
    }
    lz_dz_prev = dz;
}

void mode_delta_butterfly_on_mod_press(mod_press_event_t ev, uint32_t ts_ms) {
    (void)ev;
    (void)ts_ms;
    delta_param[4] = (uint8_t)((delta_param[4] + 1u) % LORENZ_NUM_SPEEDS);
}
