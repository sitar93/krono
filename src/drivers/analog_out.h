#ifndef ANALOG_OUT_H
#define ANALOG_OUT_H

#include <stdbool.h>
#include <stdint.h>

/**
 * Pseudo-DAC on gate outputs through the existing output stage (no hardware change).
 *
 * A ~7 MHz PWM is averaged by the R41 (10k) / op-amp input node and the slew-limited LM324 into a smooth
 * level; fine resolution comes from per-period CCR dithering streamed by DMA; a measured table linearises
 * duty -> voltage (0..~8.4 V).
 *
 *  - 1A / 2A / 2B: TIM1 CH2N / CH3N / CH1N, DMA burst on TIM1 update (repetition counter paces the DMA).
 *  - 3A: TIM5 CH3 (experimental). TIM5 has no repetition counter, so its CCR is fed by a DMA paced by TIM4
 *    (not phase-locked: a few dither values are skipped/repeated - fine for slow CV). TIM4 is shared with
 *    the audio gate engine, so 3A is CV-only (not available while audio streaming).
 *
 * While a channel is enabled its pin is in AF mode, so normal gate writes to that jack are ignored by hardware.
 */
typedef enum {
    ANALOG_OUT_1A = 0, // PB0  TIM1_CH2N
    ANALOG_OUT_2A,     // PB1  TIM1_CH3N
    ANALOG_OUT_2B,     // PB13 TIM1_CH1N
    ANALOG_OUT_3A,     // PA2  TIM5_CH3 (CV only)
    ANALOG_OUT_NUM
} analog_out_ch_t;

/** Route a channel to the PWM DAC (enable) or give the pin back to normal gate GPIO (disable). */
void analog_out_enable(analog_out_ch_t ch, bool enable);

/** Disable all channels (and audio streaming) and stop the timers / DMA. */
void analog_out_disable_all(void);

/** Set the target level 0.0..1.0 (full scale ~8.4 V). Applied at the next analog_out_update(). */
void analog_out_set(analog_out_ch_t ch, float level);

/** Refresh the dither buffers (call every main-loop iteration; work is done once per ms). */
void analog_out_update(uint32_t now_ms);

// --- Audio streaming (1A + 2A + 2B) ---

/** Audio sample rate in Hz (~36.5 kHz). */
float analog_out_audio_sample_rate(void);

/**
 * Render callback, called from the DMA interrupt: fill `n` samples (levels 0..1) for 1A, 2A and 2B.
 * Keep it short: it runs every ~220 us with n = 8.
 */
typedef void (*analog_out_audio_fn)(float *out_1a, float *out_2a, float *out_2b, uint32_t n);

/** Switch 1A, 2A and 2B to continuous audio streaming driven by `fn`. */
void analog_out_audio_start(analog_out_audio_fn fn);

/** Stop streaming and give 1A / 2A / 2B back to normal gates. */
void analog_out_audio_stop(void);

#endif
