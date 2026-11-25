#include "modes.h"
#include "mode_delta_shared.h"
#include "../drivers/io.h"
#include "../drivers/analog_out.h"
#include "../drivers/audio_gates.h"
#include "../main_constants.h"

#include <math.h>
#include <stdbool.h>

/*
 * DELTA tier, audio modes 36-40 (self-evolving, tempo-synced; a bar = 4 beats).
 *   1A / 2A / 2B: analog audio voices streamed through the pseudo-DAC (continuous phase, ~36.5 kHz).
 *   1B = base clock, 3B = counter-phase clock (as in 31-35).
 *   3A 4A 5A 6A 4B 5B 6B: 1-bit voices (squares, noise, logic, triggers) from drivers/audio_gates.c.
 * Pitch follows the tempo: the beat rate is octave-shifted into 55..110 Hz and snapped to the nearest
 * semitone (A = 55 Hz), so changing the tempo transposes the mode.
 *
 * Silence hold: generation starts AUDIO_HOLD_MS after entering a mode and after every flash save
 * (mode_delta_audio_hold): IRQs are masked while saving, which would otherwise loop the DMA buffer (whistle)
 * and freeze the 1-bit voices (bits).
 */

#define AUDIO_HOT __attribute__((optimize("O2")))
#define AUDIO_HOLD_MS 2000u

// --- Shared audio helpers ---

#define SINE_TABLE_LEN 256u
static float sine_table[SINE_TABLE_LEN + 1u];
static bool sine_built = false;
static float sample_rate = 36458.0f;
static float inc_scale = 4294967296.0f / 36458.0f; // Hz -> phase increment
#define PHASE_PER_RAD 683565275.6f                 // 2^32 / (2 pi)

static void sine_build(void) {
    if (sine_built) {
        return;
    }
    for (uint32_t i = 0; i <= SINE_TABLE_LEN; i++) {
        sine_table[i] = sinf(DELTA_TWO_PI * (float)i / (float)SINE_TABLE_LEN);
    }
    sine_built = true;
}

/** Bipolar sine -1..1 from a 32-bit phase. */
static inline float osc_sine(uint32_t phase) {
    uint32_t const i = phase >> 24;
    float const f = (float)((phase >> 8) & 0xFFFFu) * (1.0f / 65536.0f);
    return sine_table[i] + f * (sine_table[i + 1u] - sine_table[i]);
}

/** Bipolar triangle -1..1 from a 32-bit phase. */
static inline float osc_tri(uint32_t phase) {
    float const x = (float)(phase >> 8) * (1.0f / 16777216.0f);
    return (x < 0.5f) ? (4.0f * x - 1.0f) : (3.0f - 4.0f * x);
}

static inline uint32_t hz_to_inc(float hz) {
    if (hz <= 0.0f) {
        return 0u;
    }
    float const x = hz * inc_scale;
    return (x >= 2147483648.0f) ? 0x80000000u : (uint32_t)x;
}

static inline float clamp01(float v) {
    return (v < 0.0f) ? 0.0f : ((v > 1.0f) ? 1.0f : v);
}

/** Tempo -> root frequency in 55..110 Hz, snapped to the nearest semitone of A = 55 Hz. */
static float root_from_tempo(uint32_t interval_ms) {
    if (interval_ms == 0u) {
        interval_ms = 500u;
    }
    float f = 1000.0f / (float)interval_ms;
    while (f < 55.0f) {
        f *= 2.0f;
    }
    while (f >= 110.0f) {
        f *= 0.5f;
    }
    float const n = roundf(12.0f * log2f(f / 55.0f));
    return 55.0f * powf(2.0f, n / 12.0f);
}

static inline float semi(float f, float semitones) {
    return f * powf(2.0f, semitones / 12.0f);
}

/** 16-step Euclidean pattern (k hits), rotated. Bit i = step i. */
static uint16_t euclid16(uint32_t k, uint32_t rot) {
    uint16_t p = 0;
    if (k > 16u) {
        k = 16u;
    }
    for (uint32_t i = 0; i < 16u; i++) {
        if (((i * k) % 16u) < k) {
            p |= (uint16_t)(1u << ((i + rot) % 16u));
        }
    }
    return p;
}

static uint32_t audio_seed = 0x51ED270Bu;

static float rnd(void) {
    return delta_rand01(&audio_seed);
}

static uint32_t rndi(uint32_t n) {
    return (n == 0u) ? 0u : (delta_rand(&audio_seed) % n);
}

// Gate voice bursts (enable for a while, then mute)
static uint32_t burst_end_ms[AUDIO_GATES_NUM];

static void burst(audio_gate_voice_t v, uint32_t now, uint32_t dur_ms) {
    audio_gates_set_enabled(v, true);
    burst_end_ms[v] = now + (dur_ms ? dur_ms : 1u);
}

static void burst_service(uint32_t now) {
    for (uint32_t i = 0; i < AUDIO_GATES_NUM; i++) {
        if (burst_end_ms[i] != 0u && (int32_t)(now - burst_end_ms[i]) >= 0) {
            audio_gates_set_enabled((audio_gate_voice_t)i, false);
            burst_end_ms[i] = 0u;
        }
    }
}

static const audio_gate_voice_t FREE_VOICES[7] = {AG_V_3A, AG_V_4A, AG_V_5A, AG_V_6A, AG_V_4B, AG_V_5B, AG_V_6B};

static void free_voices_set(audio_gate_type_t type) {
    for (uint32_t i = 0; i < 7u; i++) {
        audio_gates_set(FREE_VOICES[i], type, 0.0f, 0u, 0u);
    }
}

// Analog voice state (written by mode updates, read by the DMA interrupt)
static volatile uint32_t av_inc[3];
static uint32_t av_phase[3];

static void av_reset(void) {
    for (uint32_t i = 0; i < 3u; i++) {
        av_phase[i] = 0u;
        av_inc[i] = 0u;
    }
}

// --- Engine lifecycle with silence hold ---

typedef void (*audio_setup_fn)(void); // Configure gate voices after (re)starting the gate engine

static analog_out_audio_fn eng_render = 0;
static audio_setup_fn eng_setup = 0;
static bool eng_active = false;  // An audio mode is selected
static bool eng_running = false; // Generators running (not in the silence hold)
static uint32_t eng_resume_ms = 0;

static void eng_stop_outputs(void) {
    analog_out_audio_stop();
    audio_gates_stop();
    delta_all_gates_off();
    eng_running = false;
}

static void eng_hold(uint32_t now) {
    if (eng_running) {
        eng_stop_outputs();
    }
    eng_resume_ms = now + AUDIO_HOLD_MS;
}

static void audio_begin(analog_out_audio_fn fn, audio_setup_fn setup) {
    sine_build();
    delta_all_gates_off();
    sample_rate = analog_out_audio_sample_rate();
    inc_scale = 4294967296.0f / sample_rate;
    eng_render = fn;
    eng_setup = setup;
    eng_active = true;
    eng_running = false;
    eng_hold(millis());
}

static void audio_end(void) {
    eng_active = false;
    eng_stop_outputs();
}

/** Beat tracking shared by all audio modes (drives the 1B / 3B clocks). */
static delta_beat_t audio_beat;

/**
 * Call first in every audio update. Handles the silence hold, clocks and bursts.
 * Returns false while silent (the mode must not generate anything).
 */
static bool audio_service(const mode_context_t *ctx, float *pos_out) {
    uint32_t const now = ctx->current_time_ms;
    float const pos = delta_beat_position(&audio_beat, ctx);
    *pos_out = pos;
    if (!eng_running) {
        if (!eng_active || (int32_t)(now - eng_resume_ms) < 0) {
            return false;
        }
        for (uint32_t i = 0; i < AUDIO_GATES_NUM; i++) {
            burst_end_ms[i] = 0u;
        }
        audio_gates_start();
        audio_gates_set(AG_V_1B, AG_HIGH, 0.0f, 0u, 0u);
        audio_gates_set(AG_V_3B, AG_HIGH, 0.0f, 0u, 0u);
        audio_gates_set_enabled(AG_V_1B, false);
        audio_gates_set_enabled(AG_V_3B, false);
        audio_gates_set(AG_V_2B, AG_OFF, 0.0f, 0u, 0u); // 2B is an analog voice
        if (eng_setup) {
            eng_setup();
        }
        analog_out_audio_start(eng_render);
        eng_running = true;
    }
    burst_service(now);
    if (ctx->f1_rising_edge) {
        burst(AG_V_1B, now, DEFAULT_PULSE_DURATION_MS);
    }
    if (delta_beat_half_crossed(&audio_beat, pos)) {
        burst(AG_V_3B, now, DEFAULT_PULSE_DURATION_MS);
    }
    return true;
}

void mode_delta_audio_hold(uint32_t now_ms) {
    if (eng_active) {
        eng_hold(now_ms);
    }
}

/** 16th-step tracker: returns true once per new 16th (s16 = absolute 16th index). */
static bool new_16th(float pos, int32_t *last, int32_t *s16) {
    *s16 = (int32_t)(pos * 4.0f);
    if (*s16 == *last) {
        return false;
    }
    *last = *s16;
    return true;
}

// =====================================================================================================
// 36 MONOLITH - evolving modal drone
//   Harmony: modal drone chords around the tonic (i add9, bVII, IV maj7, v7sus, bVI maj7).
//   Every 8-12 bars the progression moves to a neighbour chord and mostly returns home. Every voice glides
//   slowly (time constant MONO_GLIDE_BARS) to the nearest new chord tone, so changes blur instead of stepping.
//   Analog voices: 1A bass, 2A middle, 2B upper (slightly detuned). Each morphs continuously
//   sine -> saw (band-limited) -> triangle -> sine over ~32 bars (voices offset) and "breathes" in level
//   with its own very slow LFO.
//   1-bit voices are pulse waves with slowly sweeping width (PWM): 3A sub, 4A bass, 5A fifth, 6A middle +8va,
//   4B upper +8va, 5B colour tone, 6B shimmer (+2 oct). They swell in and out sparsely (narrow pulse = soft).
//   MOD: progression on / frozen (hold the current chord). Persisted.
// =====================================================================================================

#define MONO_NUM_CHORDS 5u
#define MONO_TONES 4u
/** Chord voicings in semitones from the tonic: bass, middle, upper, colour. */
static const float MONO_CHORDS[MONO_NUM_CHORDS][MONO_TONES] = {
    {0.0f, 7.0f, 14.0f, 15.0f},  // i add9
    {-2.0f, 5.0f, 12.0f, 14.0f}, // bVII sus2
    {5.0f, 12.0f, 16.0f, 19.0f}, // IV maj7 (open)
    {7.0f, 14.0f, 17.0f, 22.0f}, // v7 sus
    {-4.0f, 3.0f, 10.0f, 14.0f}, // bVI maj7
};
#define MONO_NUM_GATE 7u
/** 1-bit voice -> (chord tone index, octave offset in semitones). */
static const uint8_t MONO_GATE_TONE[MONO_NUM_GATE] = {0u, 0u, 0u, 1u, 2u, 3u, 2u};
static const float MONO_GATE_OCT[MONO_NUM_GATE] = {-12.0f, 0.0f, 7.0f, 12.0f, 12.0f, 0.0f, 24.0f};
static const float MONO_PWM_BEATS[MONO_NUM_GATE] = {52.0f, 36.0f, 44.0f, 60.0f, 28.0f, 68.0f, 40.0f};
static const float MONO_BREATH_BEATS[3] = {44.0f, 60.0f, 76.0f};
#define MONO_MORPH_BEATS 128.0f
#define MONO_GLIDE_BARS 4.0f      // Glide time constant (~95 % of the way after 3x this)
#define MONO_CHANGE_MIN_BARS 8u   // Chord changes every 8..12 bars so long glides can settle

static uint32_t mono_chord;
static uint32_t mono_next_change_bar;
static int32_t mono_last_bar;
static uint32_t mono_last_ms;
static float mono_av_semi[3];             // Current (gliding) pitch of the analog voices
static float mono_gate_semi[MONO_NUM_GATE];
static float mono_gate_level[MONO_NUM_GATE]; // 0..1 swell (mapped to pulse width)
static bool mono_gate_target_on[MONO_NUM_GATE];
static volatile float mono_morph[3];
static volatile float mono_amp[3];

/** Band-limited saw -1..1 (polyBLEP) from a 32-bit phase and increment. */
static inline float osc_saw_blep(uint32_t phase, uint32_t inc) {
    float const t = (float)phase * (1.0f / 4294967296.0f);
    float const dt = (float)inc * (1.0f / 4294967296.0f);
    float y = 2.0f * t - 1.0f;
    if (dt > 0.0f) {
        if (t < dt) {
            float const x = t / dt;
            y -= x + x - x * x - 1.0f;
        } else if (t > 1.0f - dt) {
            float const x = (t - 1.0f) / dt;
            y -= x * x + x + x + 1.0f;
        }
    }
    return y;
}

/** Morph 0..3: sine -> saw -> triangle -> sine. */
static inline float mono_wave(float m, uint32_t phase, uint32_t inc) {
    float const s = osc_sine(phase);
    if (m < 1.0f) {
        return s + m * (osc_saw_blep(phase, inc) - s);
    }
    if (m < 2.0f) {
        float const w = osc_saw_blep(phase, inc);
        return w + (m - 1.0f) * (osc_tri(phase) - w);
    }
    float const t = osc_tri(phase);
    return t + (m - 2.0f) * (s - t);
}

static AUDIO_HOT void mono_render(float *o1, float *o2, float *o3, uint32_t n) {
    float *outs[3] = {o1, o2, o3};
    for (uint32_t v = 0; v < 3u; v++) {
        float const m = mono_morph[v];
        float const a = mono_amp[v];
        uint32_t const inc = av_inc[v];
        for (uint32_t k = 0; k < n; k++) {
            av_phase[v] += inc;
            outs[v][k] = 0.5f + 0.5f * a * mono_wave(m, av_phase[v], inc);
        }
    }
}

static void mono_setup(void) {
    free_voices_set(AG_SQUARE);
    for (uint32_t i = 0; i < MONO_NUM_GATE; i++) {
        audio_gates_set_pw(FREE_VOICES[i], 0.02f);
        audio_gates_set_enabled(FREE_VOICES[i], mono_gate_level[i] > 0.02f);
    }
    mono_last_ms = 0u;
}

/** Pick the octave of `tone` closest to the current pitch (voice leading). */
static float mono_nearest(float current, float tone) {
    float best = tone;
    float best_d = 1e9f;
    for (int32_t o = -2; o <= 2; o++) {
        float const c = tone + 12.0f * (float)o;
        float const d = fabsf(c - current);
        if (d < best_d) {
            best_d = d;
            best = c;
        }
    }
    return best;
}

void mode_delta_monolith_init(void) {
    if (delta_param[5] > 1u) {
        delta_param[5] = 0u; // 0 = progression on, 1 = frozen
    }
    av_reset();
    delta_beat_reset(&audio_beat);
    mono_chord = 0u;
    mono_next_change_bar = MONO_CHANGE_MIN_BARS + rndi(5u);
    mono_last_bar = -1;
    for (uint32_t i = 0; i < 3u; i++) {
        mono_av_semi[i] = MONO_CHORDS[0][i];
        mono_morph[i] = 0.0f;
        mono_amp[i] = 0.0f;
    }
    for (uint32_t i = 0; i < MONO_NUM_GATE; i++) {
        mono_gate_semi[i] = MONO_CHORDS[0][MONO_GATE_TONE[i]] + MONO_GATE_OCT[i];
        mono_gate_level[i] = 0.0f;
        mono_gate_target_on[i] = (i == 0u || i == 4u); // start with sub and an upper pulse
    }
    audio_begin(mono_render, mono_setup);
}

void mode_delta_monolith_reset(void) {
    audio_end();
}

void mode_delta_monolith_update(const mode_context_t *ctx) {
    float pos;
    if (!audio_service(ctx, &pos)) {
        return;
    }
    uint32_t const now = ctx->current_time_ms;
    bool const frozen = (delta_param[5] == 1u);

    // --- Harmony: progression on bar boundaries ---
    int32_t const bar = audio_beat.started ? (int32_t)(audio_beat.beats / 4u) : 0;
    if (bar != mono_last_bar) {
        mono_last_bar = bar;
        if (!frozen && (uint32_t)bar >= mono_next_change_bar) {
            if (mono_chord != 0u && rnd() < 0.7f) {
                mono_chord = 0u; // mostly back home
            } else {
                mono_chord = 1u + rndi(MONO_NUM_CHORDS - 1u);
            }
            mono_next_change_bar = (uint32_t)bar + MONO_CHANGE_MIN_BARS + rndi(5u);
        }
        // Sparse swells of the pulse voices: keep 2..5 targeted on
        uint32_t const v = rndi(MONO_NUM_GATE);
        uint32_t on = 0;
        for (uint32_t i = 0; i < MONO_NUM_GATE; i++) {
            on += mono_gate_target_on[i] ? 1u : 0u;
        }
        if (mono_gate_target_on[v] && on > 2u && rnd() < 0.5f) {
            mono_gate_target_on[v] = false;
        } else if (!mono_gate_target_on[v] && on < 5u) {
            mono_gate_target_on[v] = true;
        }
    }

    // --- Continuous motion, once per ms ---
    if (now == mono_last_ms) {
        return;
    }
    float const dt = (mono_last_ms == 0u) ? 1.0f : (float)(now - mono_last_ms);
    mono_last_ms = now;
    float const beat_ms = (ctx->current_tempo_interval_ms > 0u) ? (float)ctx->current_tempo_interval_ms : 500.0f;
    float const glide = dt / (beat_ms * 4.0f * MONO_GLIDE_BARS);
    float const swell = dt / (beat_ms * 8.0f);         // ~2 bars to swell in / out
    float const root = root_from_tempo(ctx->current_tempo_interval_ms);
    float const *chord = MONO_CHORDS[mono_chord];

    static const float AV_OCT[3] = {12.0f, 12.0f, 12.0f};
    static const float AV_DETUNE[3] = {1.0f, 1.0f, 1.0025f};
    for (uint32_t i = 0; i < 3u; i++) {
        float const target = mono_nearest(mono_av_semi[i], chord[i]);
        mono_av_semi[i] += (target - mono_av_semi[i]) * (glide > 1.0f ? 1.0f : glide);
        av_inc[i] = hz_to_inc(semi(root, mono_av_semi[i] + AV_OCT[i]) * AV_DETUNE[i]);
        mono_morph[i] = fmodf(3.0f * pos / MONO_MORPH_BEATS + (float)i, 3.0f);
        float const breath = 0.5f + 0.5f * sinf(DELTA_TWO_PI * pos / MONO_BREATH_BEATS[i] + 2.1f * (float)i);
        mono_amp[i] = 0.35f + 0.65f * breath;
    }

    for (uint32_t i = 0; i < MONO_NUM_GATE; i++) {
        audio_gate_voice_t const v = FREE_VOICES[i];
        float const target = mono_nearest(mono_gate_semi[i], chord[MONO_GATE_TONE[i]] + MONO_GATE_OCT[i]);
        mono_gate_semi[i] += (target - mono_gate_semi[i]) * (glide > 1.0f ? 1.0f : glide);
        float const detune = (i & 1u) ? 1.003f : 0.998f;
        audio_gates_set_freq(v, semi(root, mono_gate_semi[i]) * detune);

        float const goal = mono_gate_target_on[i] ? 1.0f : 0.0f;
        mono_gate_level[i] += (goal - mono_gate_level[i]) * (swell > 1.0f ? 1.0f : swell);
        float const sweep = 0.5f + 0.42f * sinf(DELTA_TWO_PI * pos / MONO_PWM_BEATS[i] + (float)i);
        audio_gates_set_pw(v, 0.02f + mono_gate_level[i] * (sweep - 0.02f));
        audio_gates_set_enabled(v, mono_gate_level[i] > 0.02f);
    }
}

void mode_delta_monolith_on_mod_press(mod_press_event_t ev, uint32_t ts_ms) {
    (void)ev;
    (void)ts_ms;
    delta_param[5] = (delta_param[5] == 1u) ? 0u : 1u; // toggle progression on / frozen
}

// =====================================================================================================
// 37 STATIC - noise weather
//   The colour (filter / clock rates) drifts as a slow random walk; gusts swell the 1A/2B level over bars.
//   Characters (MOD, persisted):
//     rain   - gate noise voices cut by 16th Euclidean patterns that mutate every bar, dense dust on 2A
//     wind   - continuous voices, wide slow colour drift, strong gusts
//     radio  - voices hop between noise and random tones every beat, 2B band sweeps
//     geiger - sparse random clicks on the gate voices, sparse dust
//   1A: low-passed noise, 2A: dust (random clicks with decay), 2B: band-passed noise.
// =====================================================================================================

#define STATIC_NUM_CHARS 4u
static volatile float static_lp;      // 1A filter coefficient
static volatile float static_dust_p;  // 2A click probability per sample
static volatile float static_gain;    // Gust level 0.2..1
static volatile float static_bp_fast; // 2B band edges
static volatile float static_bp_slow;
static uint32_t static_seed = 0x2545F491u;
static float static_lp_y, static_bp_a, static_bp_b, static_dust_env;
static float static_color;            // 0 (bright) .. 3 (rumble)
static float static_gust_phase;
static uint16_t static_pattern[7];
static int32_t static_last_16th;
static int32_t static_last_bar;

static AUDIO_HOT void static_render(float *o1, float *o2, float *o3, uint32_t n) {
    float const lp = static_lp;
    float const g = static_gain;
    float const dp = static_dust_p;
    float const bf = static_bp_fast;
    float const bs = static_bp_slow;
    for (uint32_t k = 0; k < n; k++) {
        float const x = delta_rand01(&static_seed);
        static_lp_y += lp * (x - static_lp_y);
        o1[k] = clamp01(0.5f + 1.6f * g * (static_lp_y - 0.5f));
        if (delta_rand01(&static_seed) < dp) {
            static_dust_env = 0.3f + 0.7f * delta_rand01(&static_seed);
        }
        static_dust_env *= 0.996f;
        o2[k] = static_dust_env;
        static_bp_a += bf * (x - static_bp_a);
        static_bp_b += bs * (x - static_bp_b);
        o3[k] = clamp01(0.5f + 2.5f * g * (static_bp_a - static_bp_b));
    }
}

static void static_apply_color(uint8_t chr) {
    // Colour 0..3 -> rate multiplier 36 kHz .. 0.7 kHz (log), filter 0.6 .. 0.015
    float const c = static_color;
    float const rate = 36000.0f * expf(-c * 1.31f);
    static_lp = 0.6f * expf(-c * 1.23f);
    static_bp_fast = (static_lp * 1.5f > 0.9f) ? 0.9f : static_lp * 1.5f;
    static_bp_slow = static_lp * 0.3f;
    static const float scale[7] = {1.0f, 0.8f, 0.63f, 0.5f, 0.4f, 0.32f, 0.25f};
    if (chr != 2u) { // radio sets its own voice frequencies
        for (uint32_t i = 0; i < 7u; i++) {
            audio_gates_set_freq(FREE_VOICES[i], rate * scale[i]);
        }
    }
}

static void static_setup(void) {
    free_voices_set(AG_NOISE);
    uint8_t const chr = (delta_param[6] < STATIC_NUM_CHARS) ? delta_param[6] : 0u;
    for (uint32_t i = 0; i < 7u; i++) {
        audio_gates_set_enabled(FREE_VOICES[i], chr == 1u || chr == 2u);
    }
    static_apply_color(chr);
}

void mode_delta_static_init(void) {
    if (delta_param[6] >= STATIC_NUM_CHARS) {
        delta_param[6] = 0u;
    }
    delta_beat_reset(&audio_beat);
    static_lp_y = static_bp_a = static_bp_b = 0.5f;
    static_dust_env = 0.0f;
    static_color = 1.0f;
    static_gain = 0.8f;
    static_dust_p = 0.002f;
    static_gust_phase = 0.0f;
    static_last_16th = -1;
    static_last_bar = -1;
    for (uint32_t i = 0; i < 7u; i++) {
        static_pattern[i] = euclid16(3u + rndi(8u), rndi(16u));
    }
    audio_begin(static_render, static_setup);
}

void mode_delta_static_reset(void) {
    audio_end();
}

void mode_delta_static_update(const mode_context_t *ctx) {
    float pos;
    if (!audio_service(ctx, &pos)) {
        return;
    }
    uint32_t const now = ctx->current_time_ms;
    uint8_t const chr = (delta_param[6] < STATIC_NUM_CHARS) ? delta_param[6] : 0u;
    float const step_ms = (float)ctx->current_tempo_interval_ms / 4.0f;

    // Gusts: slow swell, period 2-4 bars, deeper for wind
    float const gust = 0.5f + 0.5f * sinf(DELTA_TWO_PI * pos / (chr == 1u ? 12.0f : 16.0f));
    static_gain = (chr == 1u) ? (0.2f + 0.8f * gust) : (0.6f + 0.4f * gust);
    static_dust_p = (chr == 0u) ? 0.006f : (chr == 3u) ? 0.0006f : 0.0015f;

    int32_t s16;
    if (!audio_beat.started || !new_16th(pos, &static_last_16th, &s16)) {
        return;
    }
    uint32_t const step = (uint32_t)s16 % 16u;
    int32_t const bar = s16 / 16;

    // Colour random walk (wider and slower for wind)
    float const span = (chr == 1u) ? 0.08f : 0.04f;
    static_color += (rnd() - 0.5f) * span;
    static_color = (static_color < 0.0f) ? 0.0f : ((static_color > 3.0f) ? 3.0f : static_color);
    static_apply_color(chr);

    if (bar != static_last_bar) {
        static_last_bar = bar;
        uint32_t const v = rndi(7u); // mutate one pattern per bar
        static_pattern[v] = euclid16(2u + rndi(10u), rndi(16u));
    }

    for (uint32_t i = 0; i < 7u; i++) {
        audio_gate_voice_t const v = FREE_VOICES[i];
        if (chr == 0u) { // rain: Euclidean cuts
            if (static_pattern[i] & (1u << step)) {
                burst(v, now, (uint32_t)(step_ms * 0.6f));
            }
        } else if (chr == 2u) { // radio: retune each beat, noise or tone
            if ((step % 4u) == 0u) {
                if (rnd() < 0.5f) {
                    audio_gates_set(v, AG_NOISE, 2000.0f + 20000.0f * rnd(), 0u, 0u);
                } else {
                    audio_gates_set(v, AG_SQUARE, 80.0f + 1800.0f * rnd(), 0u, 0u);
                }
            }
        } else if (chr == 3u) { // geiger: sparse clicks
            if (rnd() < 0.12f) {
                burst(v, now, 2u + rndi(6u));
            }
        }
    }
    if (chr == 2u) { // radio: sweep the 2B band
        float const sweep = 0.5f + 0.5f * sinf(DELTA_TWO_PI * pos / 8.0f);
        static_bp_fast = 0.05f + 0.8f * sweep;
        static_bp_slow = static_bp_fast * 0.25f;
    }
}

void mode_delta_static_on_mod_press(mod_press_event_t ev, uint32_t ts_ms) {
    (void)ev;
    (void)ts_ms;
    delta_param[6] = (uint8_t)((delta_param[6] + 1u) % STATIC_NUM_CHARS);
    if (eng_running) {
        static_setup();
    }
}

// =====================================================================================================
// 38 PULSAR - evolving 1-bit drum machine (bar of 16ths)
//   Genre patterns mutate every 4 bars (hats / toms / blips / zaps freely, kick & snare rarely); a fill on
//   the last beat of every 8th bar; ghost snares; kick pitch and swing drift slowly.
//   1A kick, 2A zap, 2B tom (analog); 3A closed hat, 4A snare, 5A blip, 6A open hat (1-bit);
//   4B kick trigger, 5B snare trigger, 6B glitch blips.
//   MOD: genre (techno, broken, half-time, glitch) - also picks the kit. Persisted.
// =====================================================================================================

typedef struct {
    float kick_hi, kick_lo, kick_pitch_ms, kick_amp_ms;
    float zap_hi, zap_lo, zap_pitch_ms, zap_amp_ms;
    uint32_t hat_ms, snare_ms, open_ms, blip_ms;
} pulsar_kit_t;

#define PULSAR_NUM_GENRES 4u
static const pulsar_kit_t PULSAR_KITS[PULSAR_NUM_GENRES] = {
    {160.0f, 48.0f, 40.0f, 300.0f, 600.0f, 200.0f, 15.0f, 80.0f, 25u, 120u, 90u, 30u},   // techno
    {200.0f, 60.0f, 20.0f, 120.0f, 1200.0f, 400.0f, 10.0f, 40.0f, 12u, 60u, 45u, 15u},   // broken
    {120.0f, 40.0f, 80.0f, 600.0f, 300.0f, 100.0f, 30.0f, 200.0f, 40u, 200u, 160u, 50u}, // half-time
    {400.0f, 80.0f, 8.0f, 60.0f, 3000.0f, 800.0f, 5.0f, 25.0f, 8u, 30u, 20u, 8u},        // glitch
};

enum { PI_KICK = 0, PI_SNARE, PI_HAT, PI_OPEN, PI_TOM, PI_BLIP, PI_ZAP, PI_NUM };
/** Base patterns per genre (bit i = 16th step i). */
static const uint16_t PULSAR_BASE[PULSAR_NUM_GENRES][PI_NUM] = {
    {0x1111u, 0x1010u, 0x5555u, 0x4444u, 0x0808u, 0x2424u, 0x4444u}, // techno
    {0x0491u, 0x1010u, 0x5D55u, 0x0400u, 0x4820u, 0x2402u, 0x8888u}, // broken
    {0x0081u, 0x0100u, 0x5555u, 0x4040u, 0x8008u, 0x0420u, 0x4004u}, // half-time
    {0x1249u, 0x9010u, 0xB6DBu, 0x0420u, 0x2492u, 0x5A5Au, 0xA5A5u}, // glitch
};

typedef struct {
    float hi, lo, penv, aenv, kp, ka;
} perc_voice_t;

static perc_voice_t perc[3]; // kick (1A), zap (2A), tom (2B)
static volatile bool perc_trigger[3];
static uint16_t pulsar_pat[PI_NUM];
static int32_t pulsar_last_step_abs;
static int32_t pulsar_last_bar;
static uint8_t pulsar_genre;
static float pulsar_swing;

static void perc_set(perc_voice_t *p, float hi, float lo, float pitch_ms, float amp_ms) {
    p->hi = hi;
    p->lo = lo;
    p->kp = expf(-1000.0f / (pitch_ms * sample_rate));
    p->ka = expf(-1000.0f / (amp_ms * sample_rate));
}

static void perc_setup(uint8_t kit, float kick_tune) {
    pulsar_kit_t const *k = &PULSAR_KITS[kit];
    perc_set(&perc[0], k->kick_hi * kick_tune, k->kick_lo * kick_tune, k->kick_pitch_ms, k->kick_amp_ms);
    perc_set(&perc[1], k->zap_hi, k->zap_lo, k->zap_pitch_ms, k->zap_amp_ms);
    perc_set(&perc[2], k->kick_hi * 1.8f, k->kick_lo * 2.2f, k->kick_pitch_ms * 1.5f, k->kick_amp_ms * 0.6f);
}

static AUDIO_HOT void pulsar_render(float *o1, float *o2, float *o3, uint32_t n) {
    float *outs[3] = {o1, o2, o3};
    for (uint32_t v = 0; v < 3u; v++) {
        perc_voice_t *p = &perc[v];
        if (perc_trigger[v]) {
            perc_trigger[v] = false;
            p->penv = 1.0f;
            p->aenv = 1.0f;
            av_phase[v] = 0u;
        }
        float const span = p->hi - p->lo;
        for (uint32_t k = 0; k < n; k++) {
            av_phase[v] += hz_to_inc(p->lo + span * p->penv);
            outs[v][k] = 0.5f + 0.5f * p->aenv * osc_sine(av_phase[v]);
            p->penv *= p->kp;
            p->aenv *= p->ka;
        }
    }
}

static void pulsar_load_genre(uint8_t g) {
    pulsar_genre = g;
    for (uint32_t i = 0; i < PI_NUM; i++) {
        pulsar_pat[i] = PULSAR_BASE[g][i];
    }
    perc_setup(g, 1.0f);
}

static void pulsar_mutate(void) {
    for (uint32_t i = 0; i < PI_NUM; i++) {
        float const p = (i == PI_KICK || i == PI_SNARE) ? 0.15f : 0.6f;
        if (rnd() < p) {
            uint32_t const flips = 1u + rndi(2u);
            for (uint32_t f = 0; f < flips; f++) {
                pulsar_pat[i] ^= (uint16_t)(1u << rndi(16u));
            }
        }
    }
    pulsar_pat[PI_KICK] |= 0x0001u; // Keep the downbeat
}

static void pulsar_setup(void) {
    audio_gates_set(AG_V_3A, AG_NOISE, 30000.0f, 0u, 0u); // closed hat
    audio_gates_set(AG_V_4A, AG_NOISE, 9000.0f, 0u, 0u);  // snare
    audio_gates_set(AG_V_5A, AG_SQUARE, 1760.0f, 0u, 0u); // blip
    audio_gates_set(AG_V_6A, AG_NOISE, 20000.0f, 0u, 0u); // open hat
    audio_gates_set(AG_V_4B, AG_HIGH, 0.0f, 0u, 0u);      // kick trigger
    audio_gates_set(AG_V_5B, AG_HIGH, 0.0f, 0u, 0u);      // snare trigger
    audio_gates_set(AG_V_6B, AG_SQUARE, 3520.0f, 0u, 0u); // glitch blip
    for (uint32_t i = 0; i < 7u; i++) {
        audio_gates_set_enabled(FREE_VOICES[i], false);
    }
}

void mode_delta_pulsar_init(void) {
    if (delta_param[7] >= PULSAR_NUM_GENRES) {
        delta_param[7] = 0u;
    }
    for (uint32_t v = 0; v < 3u; v++) {
        perc[v].penv = 0.0f;
        perc[v].aenv = 0.0f;
        perc_trigger[v] = false;
    }
    av_reset();
    delta_beat_reset(&audio_beat);
    sample_rate = analog_out_audio_sample_rate();
    pulsar_load_genre(delta_param[7]);
    pulsar_last_step_abs = -1;
    pulsar_last_bar = -1;
    pulsar_swing = 0.0f;
    audio_begin(pulsar_render, pulsar_setup);
}

void mode_delta_pulsar_reset(void) {
    audio_end();
}

void mode_delta_pulsar_update(const mode_context_t *ctx) {
    float pos;
    if (!audio_service(ctx, &pos) || !audio_beat.started) {
        return;
    }
    uint32_t const now = ctx->current_time_ms;
    uint8_t const genre = (delta_param[7] < PULSAR_NUM_GENRES) ? delta_param[7] : 0u;
    if (genre != pulsar_genre) {
        pulsar_load_genre(genre);
    }
    pulsar_kit_t const *k = &PULSAR_KITS[genre];

    // Swing drifts 0..22 % of a 16th over 16 bars; odd 16ths are delayed
    pulsar_swing = 0.11f + 0.11f * sinf(DELTA_TWO_PI * pos / 64.0f);
    float const q = pos * 4.0f;
    int32_t const s16 = (int32_t)q;
    float const frac = q - (float)s16;
    bool const odd = (s16 & 1) != 0;
    if (s16 == pulsar_last_step_abs || (odd && frac < pulsar_swing)) {
        return;
    }
    pulsar_last_step_abs = s16;
    uint32_t const step = (uint32_t)s16 % 16u;
    int32_t const bar = s16 / 16;

    if (bar != pulsar_last_bar) {
        pulsar_last_bar = bar;
        if ((bar % 4) == 0 && bar > 0) {
            pulsar_mutate();
        }
        // Kick tuning drifts per bar (+-15 %)
        perc_setup(genre, 1.0f + 0.15f * sinf(DELTA_TWO_PI * (float)bar / 13.0f));
    }

    float const r = root_from_tempo(ctx->current_tempo_interval_ms);
    bool const fill = ((bar % 8) == 7) && step >= 12u;
    uint16_t const bit = (uint16_t)(1u << step);

    if ((pulsar_pat[PI_KICK] & bit) && !fill) {
        perc_trigger[0] = true;
        burst(AG_V_4B, now, DEFAULT_PULSE_DURATION_MS);
    }
    if ((pulsar_pat[PI_SNARE] & bit) || fill) {
        burst(AG_V_4A, now, fill ? k->snare_ms / 2u : k->snare_ms);
        burst(AG_V_5B, now, DEFAULT_PULSE_DURATION_MS);
    } else if (rnd() < 0.08f) { // ghost snare
        burst(AG_V_4A, now, k->snare_ms / 4u);
    }
    if (pulsar_pat[PI_HAT] & bit) {
        burst(AG_V_3A, now, k->hat_ms);
    }
    if (pulsar_pat[PI_OPEN] & bit) {
        burst(AG_V_6A, now, k->open_ms);
    }
    if ((pulsar_pat[PI_TOM] & bit) || (fill && (step & 1u))) {
        perc_trigger[2] = true;
    }
    if (pulsar_pat[PI_ZAP] & bit) {
        perc_trigger[1] = true;
    }
    if (pulsar_pat[PI_BLIP] & bit) {
        audio_gates_set_freq(AG_V_5A, r * 16.0f);
        burst(AG_V_5A, now, k->blip_ms);
    }
    if (rnd() < 0.2f) {
        audio_gates_set_freq(AG_V_6B, r * (8.0f + 24.0f * rnd()));
        burst(AG_V_6B, now, k->blip_ms);
    }
}

void mode_delta_pulsar_on_mod_press(mod_press_event_t ev, uint32_t ts_ms) {
    (void)ev;
    (void)ts_ms;
    delta_param[7] = (uint8_t)((delta_param[7] + 1u) % PULSAR_NUM_GENRES);
}

// =====================================================================================================
// 39 ALLOY - drifting two-oscillator metal
//   The ratio f2/f1 glides to a new interval every N bars (slow 8, medium 4, fast 2, jumpy 1 = instant).
//   2B is phase-modulated by f2 with a depth that breathes over 4 bars. The logic gates (5A 6A 5B 6B) are cut
//   by 16th Euclidean patterns that mutate every bar.
//   1A: sum of the two sines, 2A: ring modulation, 2B: PM voice.
//   3A f1, 4A f2, 4B f2/2 squares; 5A XOR, 6A AND, 5B f1 XOR f2/2, 6B (XOR) XOR (f2/2).
//   MOD: evolution speed (slow, medium, fast, jumpy). Persisted.
// =====================================================================================================

#define ALLOY_NUM_SPEEDS 4u
#define ALLOY_NUM_RATIOS 8u
static const float ALLOY_RATIOS[ALLOY_NUM_RATIOS] = {
    1.5f, 1.25f, 1.41421356f, 2.71828183f, 1.33333333f, 1.66666667f, 2.0f, 1.05946309f,
};
static const uint32_t ALLOY_BARS[ALLOY_NUM_SPEEDS] = {8u, 4u, 2u, 1u};
static volatile float alloy_pm_depth;
static float alloy_ratio, alloy_target;
static uint32_t alloy_next_bar;
static int32_t alloy_last_bar;
static int32_t alloy_last_16th;
static uint16_t alloy_cut[4];
static uint32_t alloy_last_ms;

static AUDIO_HOT void alloy_render(float *o1, float *o2, float *o3, uint32_t n) {
    float const depth = alloy_pm_depth * PHASE_PER_RAD;
    for (uint32_t k = 0; k < n; k++) {
        av_phase[0] += av_inc[0];
        av_phase[1] += av_inc[1];
        float const a = osc_sine(av_phase[0]);
        float const b = osc_sine(av_phase[1]);
        o1[k] = 0.5f + 0.25f * (a + b);
        o2[k] = 0.5f + 0.5f * (a * b);
        o3[k] = 0.5f + 0.5f * osc_sine(av_phase[0] + (uint32_t)(int32_t)(depth * b));
    }
}

static void alloy_setup(void) {
    audio_gates_set(AG_V_3A, AG_SQUARE, 0.0f, 0u, 0u);
    audio_gates_set(AG_V_4A, AG_SQUARE, 0.0f, 0u, 0u);
    audio_gates_set(AG_V_4B, AG_SQUARE, 0.0f, 0u, 0u);
    audio_gates_set(AG_V_5A, AG_XOR, 0.0f, AG_V_3A, AG_V_4A);
    audio_gates_set(AG_V_6A, AG_AND, 0.0f, AG_V_3A, AG_V_4A);
    audio_gates_set(AG_V_5B, AG_XOR, 0.0f, AG_V_3A, AG_V_4B);
    audio_gates_set(AG_V_6B, AG_XOR, 0.0f, AG_V_5A, AG_V_4B);
}

void mode_delta_alloy_init(void) {
    if (delta_param[8] >= ALLOY_NUM_SPEEDS) {
        delta_param[8] = 0u;
    }
    av_reset();
    delta_beat_reset(&audio_beat);
    alloy_ratio = alloy_target = ALLOY_RATIOS[0];
    alloy_next_bar = 1u;
    alloy_last_bar = -1;
    alloy_last_16th = -1;
    alloy_pm_depth = 0.0f;
    alloy_last_ms = 0u;
    for (uint32_t i = 0; i < 4u; i++) {
        alloy_cut[i] = 0xFFFFu;
    }
    audio_begin(alloy_render, alloy_setup);
}

void mode_delta_alloy_reset(void) {
    audio_end();
}

void mode_delta_alloy_update(const mode_context_t *ctx) {
    float pos;
    if (!audio_service(ctx, &pos)) {
        return;
    }
    uint32_t const now = ctx->current_time_ms;
    uint8_t const speed = (delta_param[8] < ALLOY_NUM_SPEEDS) ? delta_param[8] : 0u;
    int32_t const bar = audio_beat.started ? (int32_t)(audio_beat.beats / 4u) : 0;

    if (bar != alloy_last_bar) {
        alloy_last_bar = bar;
        if ((uint32_t)bar >= alloy_next_bar) {
            alloy_target = ALLOY_RATIOS[rndi(ALLOY_NUM_RATIOS)];
            alloy_next_bar = (uint32_t)bar + ALLOY_BARS[speed];
            if (speed == 3u) {
                alloy_ratio = alloy_target; // jumpy
            }
        }
        alloy_cut[rndi(4u)] = euclid16(4u + rndi(12u), rndi(16u));
    }

    // Glide toward the target ratio (time constant = half the change interval), once per ms
    if (now != alloy_last_ms) {
        float const beat_ms = (float)ctx->current_tempo_interval_ms;
        float const tau_ms = beat_ms * 4.0f * (float)ALLOY_BARS[speed] * 0.5f;
        float const dt = (alloy_last_ms == 0u) ? 0.0f : (float)(now - alloy_last_ms);
        alloy_last_ms = now;
        if (tau_ms > 1.0f) {
            alloy_ratio += (alloy_target - alloy_ratio) * (dt / tau_ms > 1.0f ? 1.0f : dt / tau_ms);
        }
        float const f1 = root_from_tempo(ctx->current_tempo_interval_ms) * 2.0f;
        float const f2 = f1 * alloy_ratio;
        av_inc[0] = hz_to_inc(f1);
        av_inc[1] = hz_to_inc(f2);
        audio_gates_set_freq(AG_V_3A, f1);
        audio_gates_set_freq(AG_V_4A, f2);
        audio_gates_set_freq(AG_V_4B, f2 * 0.5f);
        alloy_pm_depth = 2.5f * (0.5f + 0.5f * sinf(DELTA_TWO_PI * pos / 16.0f));
    }

    int32_t s16;
    if (audio_beat.started && new_16th(pos, &alloy_last_16th, &s16)) {
        uint32_t const step = (uint32_t)s16 % 16u;
        static const audio_gate_voice_t cut_voices[4] = {AG_V_5A, AG_V_6A, AG_V_5B, AG_V_6B};
        for (uint32_t i = 0; i < 4u; i++) {
            audio_gates_set_enabled(cut_voices[i], (alloy_cut[i] >> step) & 1u);
        }
    }
}

void mode_delta_alloy_on_mod_press(mod_press_event_t ev, uint32_t ts_ms) {
    (void)ev;
    (void)ts_ms;
    delta_param[8] = (uint8_t)((delta_param[8] + 1u) % ALLOY_NUM_SPEEDS);
}

// =====================================================================================================
// 40 ASCENT - mutating arpeggiator (16th notes, 8-step pattern)
//   One pattern note mutates every bar; the root follows a progression (I vi IV V, with random detours)
//   every 2 bars; the octave range breathes (1-2 octaves, 8-bar cycle); density cycles over 4 bars with
//   rests and ratchets (double pluck).
//   1A plucked sine, 2A triangle -1 oct, 2B sine canon (previous note);
//   3A note, 4A +1 oct, 5A +fifth, 6A -1 oct (squares); 4B step trigger, 5B bar trigger, 6B root drone.
//   MOD: scale (major, minor, pentatonic, whole-tone). Persisted.
// =====================================================================================================

#define ASCENT_NUM_SCALES 4u
#define ASCENT_STEPS 8u
static const int8_t ASCENT_SCALES[ASCENT_NUM_SCALES][7] = {
    {0, 2, 4, 5, 7, 9, 11},
    {0, 2, 3, 5, 7, 8, 10},
    {0, 2, 4, 7, 9, 12, 14},
    {0, 2, 4, 6, 8, 10, 12},
};
static const int8_t ASCENT_PROGRESSION[4] = {0, 9, 5, 7}; // I vi IV V (semitones)
static int8_t ascent_pattern[ASCENT_STEPS]; // scale-degree indices 0..13 (two octaves)
static int32_t ascent_last_16th;
static int32_t ascent_last_bar;
static int32_t ascent_prog_idx;
static float ascent_prev_note;
static volatile bool ascent_pluck;
static volatile uint32_t ascent_ratchet_at; // samples until a second pluck (0 = none)
static float ascent_env;
static volatile float ascent_decay;
static uint32_t ascent_sample_count;
static uint8_t ascent_scale;

static AUDIO_HOT void ascent_render(float *o1, float *o2, float *o3, uint32_t n) {
    if (ascent_pluck) {
        ascent_pluck = false;
        ascent_env = 1.0f;
        ascent_sample_count = 0u;
    }
    float const d = ascent_decay;
    uint32_t const ratchet = ascent_ratchet_at;
    for (uint32_t k = 0; k < n; k++) {
        av_phase[0] += av_inc[0];
        av_phase[1] += av_inc[1];
        av_phase[2] += av_inc[2];
        if (ratchet != 0u && ascent_sample_count == ratchet) {
            ascent_env = 0.8f;
        }
        ascent_sample_count++;
        o1[k] = 0.5f + 0.5f * ascent_env * osc_sine(av_phase[0]);
        o2[k] = 0.5f + 0.5f * osc_tri(av_phase[1]);
        o3[k] = 0.5f + 0.5f * osc_sine(av_phase[2]);
        ascent_env *= d;
    }
}

static float ascent_degree_to_semi(uint8_t scale, int32_t deg) {
    int32_t const idx = ((deg % 7) + 7) % 7;
    int32_t const oct = (deg - idx) / 7;
    return (float)(ASCENT_SCALES[scale][idx] + 12 * oct);
}

static void ascent_reset_pattern(void) {
    static const int8_t base[ASCENT_STEPS] = {0, 2, 4, 7, 9, 7, 4, 2};
    for (uint32_t i = 0; i < ASCENT_STEPS; i++) {
        ascent_pattern[i] = base[i];
    }
}

static void ascent_setup(void) {
    free_voices_set(AG_SQUARE);
    audio_gates_set(AG_V_4B, AG_HIGH, 0.0f, 0u, 0u);
    audio_gates_set(AG_V_5B, AG_HIGH, 0.0f, 0u, 0u);
    audio_gates_set_enabled(AG_V_4B, false);
    audio_gates_set_enabled(AG_V_5B, false);
}

void mode_delta_ascent_init(void) {
    if (delta_param[9] >= ASCENT_NUM_SCALES) {
        delta_param[9] = 0u;
    }
    av_reset();
    delta_beat_reset(&audio_beat);
    ascent_env = 0.0f;
    ascent_decay = 0.9995f;
    ascent_pluck = false;
    ascent_ratchet_at = 0u;
    ascent_last_16th = -1;
    ascent_last_bar = -1;
    ascent_prog_idx = 0;
    ascent_prev_note = 0.0f;
    ascent_scale = delta_param[9];
    ascent_reset_pattern();
    audio_begin(ascent_render, ascent_setup);
}

void mode_delta_ascent_reset(void) {
    audio_end();
}

void mode_delta_ascent_update(const mode_context_t *ctx) {
    float pos;
    if (!audio_service(ctx, &pos) || !audio_beat.started) {
        return;
    }
    uint32_t const now = ctx->current_time_ms;
    int32_t s16;
    if (!new_16th(pos, &ascent_last_16th, &s16)) {
        return;
    }
    uint8_t const scale = (delta_param[9] < ASCENT_NUM_SCALES) ? delta_param[9] : 0u;
    if (scale != ascent_scale) {
        ascent_scale = scale;
        ascent_reset_pattern();
    }
    uint32_t const step = (uint32_t)s16 % 16u;
    int32_t const bar = s16 / 16;

    if (bar != ascent_last_bar) {
        ascent_last_bar = bar;
        ascent_pattern[rndi(ASCENT_STEPS)] = (int8_t)rndi(14u); // mutate one note
        if ((bar % 2) == 0) {
            ascent_prog_idx = (rnd() < 0.8f) ? (ascent_prog_idx + 1) % 4 : (int32_t)rndi(4u);
        }
        burst(AG_V_5B, now, DEFAULT_PULSE_DURATION_MS);
    }

    // Density cycle (4 bars): rests when sparse, ratchets when dense
    float const density = 0.5f + 0.5f * sinf(DELTA_TWO_PI * pos / 16.0f);
    if (rnd() < (1.0f - density) * 0.5f) {
        return; // rest
    }

    // Octave range breathes between 1 and 2 octaves (8-bar cycle): fold degrees above the range
    int32_t const range = 7 + (int32_t)(7.0f * (0.5f + 0.5f * sinf(DELTA_TWO_PI * pos / 32.0f)));
    int32_t deg = ascent_pattern[(uint32_t)s16 % ASCENT_STEPS];
    while (deg > range) {
        deg -= 7;
    }
    float const root = semi(root_from_tempo(ctx->current_tempo_interval_ms) * 2.0f,
                            (float)ASCENT_PROGRESSION[ascent_prog_idx]);
    float const note = ascent_degree_to_semi(scale, deg);
    float const f = semi(root, note);
    float const fp = semi(root, ascent_prev_note);

    float const step_s = (float)ctx->current_tempo_interval_ms / 4000.0f;
    ascent_decay = expf(-3.0f / (step_s * sample_rate));
    ascent_ratchet_at = (rnd() < density * 0.25f) ? (uint32_t)(step_s * sample_rate * 0.5f) : 0u;
    av_inc[0] = hz_to_inc(f);
    av_inc[1] = hz_to_inc(f * 0.5f);
    av_inc[2] = hz_to_inc(fp);
    ascent_pluck = true;

    audio_gates_set_freq(AG_V_3A, f);
    audio_gates_set_freq(AG_V_4A, f * 2.0f);
    audio_gates_set_freq(AG_V_5A, f * 1.5f);
    audio_gates_set_freq(AG_V_6A, f * 0.5f);
    audio_gates_set_freq(AG_V_6B, root * 0.5f);
    ascent_prev_note = note;

    burst(AG_V_4B, now, DEFAULT_PULSE_DURATION_MS);
    (void)step;
}

void mode_delta_ascent_on_mod_press(mod_press_event_t ev, uint32_t ts_ms) {
    (void)ev;
    (void)ts_ms;
    delta_param[9] = (uint8_t)((delta_param[9] + 1u) % ASCENT_NUM_SCALES);
}
