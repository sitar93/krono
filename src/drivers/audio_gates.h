#ifndef AUDIO_GATES_H
#define AUDIO_GATES_H

#include <stdbool.h>
#include <stdint.h>

/**
 * 1-bit audio voices on the 10 gate outputs that are not pseudo-DAC channels
 * (3A 4A 5A 6A 1B 2B 3B 4B 5B 6B), rendered by a TIM4 interrupt at AUDIO_GATES_RATE_HZ.
 */
#define AUDIO_GATES_NUM 10u
#define AUDIO_GATES_RATE_HZ 40000u

/** Voice index -> jack: 0=3A 1=4A 2=5A 3=6A 4=1B 5=2B 6=3B 7=4B 8=5B 9=6B. */
typedef enum {
    AG_V_3A = 0, AG_V_4A, AG_V_5A, AG_V_6A, AG_V_1B, AG_V_2B, AG_V_3B, AG_V_4B, AG_V_5B, AG_V_6B
} audio_gate_voice_t;

typedef enum {
    AG_OFF = 0,  // Low
    AG_HIGH,     // Constant high (use for triggers / gates)
    AG_SQUARE,   // 50 % square at `freq_hz`
    AG_NOISE,    // 1-bit LFSR noise, new bit at `freq_hz`
    AG_XOR,      // Logic of two other voices' current bits (src_a, src_b)
    AG_AND,
    AG_OR,
} audio_gate_type_t;

void audio_gates_start(void);
void audio_gates_stop(void);
bool audio_gates_running(void);

/** Configure a voice. For logic types, freq is ignored and src_a / src_b are voice indices. */
void audio_gates_set(audio_gate_voice_t v, audio_gate_type_t type, float freq_hz, uint8_t src_a, uint8_t src_b);

/** Change only the frequency (phase continuous). */
void audio_gates_set_freq(audio_gate_voice_t v, float freq_hz);

/** Square pulse width 0.01..0.99 (default 0.5). Narrow pulses sound thinner and quieter. */
void audio_gates_set_pw(audio_gate_voice_t v, float width);

/** Mute / unmute a voice without losing its settings (bursts, envelopes). */
void audio_gates_set_enabled(audio_gate_voice_t v, bool on);

#endif
