#include "analog_out.h"

#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/timer.h>
#include <libopencm3/stm32/dma.h>
#include <libopencm3/cm3/nvic.h>

/** Hot paths (DMA interrupt, sample expansion) are built at O2 regardless of the project -O1. */
#define AO_HOT __attribute__((optimize("O2")))

// --- PWM: 84 MHz / 12 = 7 MHz (measured best config) ---
#define AO_PWM_PERIOD_TICKS 12u
#define AO_CV_RCR 2u        // CV: DMA burst every 3 PWM periods (~2.33 MHz)
#define AO_AUDIO_RCR 5u     // Audio: every 6 periods (~1.17 MHz) -> half the expansion work per sample
#define AO_DITHER_LEN 64u
#define AO_BURST_REGS 3u    // CCR1, CCR2, CCR3
#define AO_TIM1_CCR1_OFFSET_WORDS 13u // CCR1 at 0x34 / 4 (DCR.DBA)
#define AO_TIM4_PACE_TICKS 36u        // 3A: TIM4 update paces the TIM5 CCR DMA at ~2.33 MHz

/** Hardware binding per channel (3A handled separately on TIM5). */
typedef struct {
    uint32_t port;
    uint16_t pin;
    enum tim_oc_id oc_n;  // Complementary output enable (CCxNE)
    uint8_t burst_slot;   // Index in the CCR1..CCR3 burst
} ao_hw_t;

static const ao_hw_t AO_HW[3] = {
    [ANALOG_OUT_1A] = {GPIOB, GPIO0, TIM_OC2N, 1u},
    [ANALOG_OUT_2A] = {GPIOB, GPIO1, TIM_OC3N, 2u},
    [ANALOG_OUT_2B] = {GPIOB, GPIO13, TIM_OC1N, 0u},
};

/**
 * Measured on hardware (1A, scope): duty fraction for output levels 0.0, 0.1, ... 1.0. The stage gain rises
 * at high levels and saturates above ~80 % duty; levels are scaled to AO_AMPLITUDE to stay below the knee.
 */
static const float AO_CAL_DUTY[11] = {
    0.000f, 0.120f, 0.267f, 0.392f, 0.491f, 0.560f, 0.623f, 0.669f, 0.711f, 0.750f, 0.780f,
};
#define AO_AMPLITUDE 0.93f

static volatile uint32_t dither_buf[AO_DITHER_LEN * AO_BURST_REGS];
static volatile uint32_t dither_3a[AO_DITHER_LEN];
static float target_level[ANALOG_OUT_NUM];
static bool ch_enabled[ANALOG_OUT_NUM];
static bool tim1_running = false;
static bool tim5_running = false;
static uint32_t last_update_ms = 0;

// Audio streaming: two halves of AO_AUDIO_SAMPLES_PER_HALF samples, each expanded to AO_AUDIO_EXPAND bursts
#define AO_AUDIO_EXPAND 32u
#define AO_AUDIO_SAMPLES_PER_HALF 8u
#define AO_AUDIO_HALF_WORDS (AO_AUDIO_SAMPLES_PER_HALF * AO_AUDIO_EXPAND * AO_BURST_REGS)
static volatile uint32_t audio_buf[2u * AO_AUDIO_HALF_WORDS];
static analog_out_audio_fn audio_fn = 0;
static bool audio_running = false;
static uint32_t audio_acc[3];

/** Level 0..1 -> fractional duty in ticks through the calibration table. */
static AO_HOT float level_to_duty(float level) {
    if (level <= 0.0f) {
        return 0.0f;
    }
    if (level > 1.0f) {
        level = 1.0f;
    }
    float const x = level * AO_AMPLITUDE * 10.0f;
    uint32_t i = (uint32_t)x;
    if (i >= 10u) {
        i = 9u;
    }
    float const f = x - (float)i;
    return (AO_CAL_DUTY[i] + f * (AO_CAL_DUTY[i + 1u] - AO_CAL_DUTY[i])) * (float)AO_PWM_PERIOD_TICKS;
}

/** Dither sequence (first-order error feedback) with a stride (burst slots). */
static void fill_dither(volatile uint32_t *dst, uint32_t stride, float duty) {
    uint32_t const base = (uint32_t)duty;
    float const frac = duty - (float)base;
    float acc = 0.0f;
    for (uint32_t i = 0; i < AO_DITHER_LEN; i++) {
        acc += frac;
        uint32_t v = base;
        if (acc >= 1.0f) {
            acc -= 1.0f;
            v++;
        }
        dst[i * stride] = v;
    }
}

static void fill_channel(analog_out_ch_t ch, float duty) {
    if (ch == ANALOG_OUT_3A) {
        fill_dither(dither_3a, 1u, duty);
    } else {
        fill_dither(&dither_buf[AO_HW[ch].burst_slot], AO_BURST_REGS, duty);
    }
}

// --- TIM1 (1A / 2A / 2B) ---

static void tim1_start(volatile uint32_t *buf, uint32_t words, uint32_t rcr, bool irq) {
    for (uint32_t i = 0; i < words; i++) {
        buf[i] = 0u;
    }
    rcc_periph_clock_enable(RCC_TIM1);
    rcc_periph_reset_pulse(RST_TIM1);
    timer_set_mode(TIM1, TIM_CR1_CKD_CK_INT, TIM_CR1_CMS_EDGE, TIM_CR1_DIR_UP);
    timer_set_prescaler(TIM1, 0);
    timer_set_period(TIM1, AO_PWM_PERIOD_TICKS - 1u);
    timer_set_repetition_counter(TIM1, rcr);
    timer_enable_preload(TIM1);
    const enum tim_oc_id ocs[3] = {TIM_OC1, TIM_OC2, TIM_OC3};
    for (uint32_t i = 0; i < 3u; i++) {
        timer_set_oc_mode(TIM1, ocs[i], TIM_OCM_PWM1);
        timer_enable_oc_preload(TIM1, ocs[i]);
        timer_set_oc_value(TIM1, ocs[i], 0);
    }
    timer_enable_break_main_output(TIM1);
    TIM_DCR(TIM1) = ((AO_BURST_REGS - 1u) << 8) | AO_TIM1_CCR1_OFFSET_WORDS; // Burst CCR1..CCR3 via DMAR

    // DMA2 Stream5 Channel6 = TIM1_UP
    rcc_periph_clock_enable(RCC_DMA2);
    dma_stream_reset(DMA2, DMA_STREAM5);
    dma_channel_select(DMA2, DMA_STREAM5, DMA_SxCR_CHSEL_6);
    dma_set_priority(DMA2, DMA_STREAM5, DMA_SxCR_PL_HIGH);
    dma_set_transfer_mode(DMA2, DMA_STREAM5, DMA_SxCR_DIR_MEM_TO_PERIPHERAL);
    dma_set_memory_size(DMA2, DMA_STREAM5, DMA_SxCR_MSIZE_32BIT);
    dma_set_peripheral_size(DMA2, DMA_STREAM5, DMA_SxCR_PSIZE_32BIT);
    dma_enable_memory_increment_mode(DMA2, DMA_STREAM5);
    dma_enable_circular_mode(DMA2, DMA_STREAM5);
    dma_set_peripheral_address(DMA2, DMA_STREAM5, (uint32_t)&TIM_DMAR(TIM1));
    dma_set_memory_address(DMA2, DMA_STREAM5, (uint32_t)buf);
    dma_set_number_of_data(DMA2, DMA_STREAM5, words);
    if (irq) {
        dma_enable_half_transfer_interrupt(DMA2, DMA_STREAM5);
        dma_enable_transfer_complete_interrupt(DMA2, DMA_STREAM5);
        nvic_enable_irq(NVIC_DMA2_STREAM5_IRQ);
    }
    dma_enable_stream(DMA2, DMA_STREAM5);
    timer_enable_irq(TIM1, TIM_DIER_UDE);
    timer_generate_event(TIM1, TIM_EGR_UG);
    timer_enable_counter(TIM1);
    tim1_running = true;
}

static void tim1_stop(void) {
    timer_disable_irq(TIM1, TIM_DIER_UDE);
    nvic_disable_irq(NVIC_DMA2_STREAM5_IRQ);
    dma_disable_half_transfer_interrupt(DMA2, DMA_STREAM5);
    dma_disable_transfer_complete_interrupt(DMA2, DMA_STREAM5);
    dma_disable_stream(DMA2, DMA_STREAM5);
    timer_disable_counter(TIM1);
    rcc_periph_clock_disable(RCC_TIM1);
    tim1_running = false;
}

// --- TIM5 CH3 (3A), CCR fed by DMA1 Stream6 Channel2 = TIM4_UP ---

static void tim5_start(void) {
    for (uint32_t i = 0; i < AO_DITHER_LEN; i++) {
        dither_3a[i] = 0u;
    }
    rcc_periph_clock_enable(RCC_TIM5);
    rcc_periph_reset_pulse(RST_TIM5);
    timer_set_mode(TIM5, TIM_CR1_CKD_CK_INT, TIM_CR1_CMS_EDGE, TIM_CR1_DIR_UP);
    timer_set_prescaler(TIM5, 0);
    timer_set_period(TIM5, AO_PWM_PERIOD_TICKS - 1u);
    timer_enable_preload(TIM5);
    timer_set_oc_mode(TIM5, TIM_OC3, TIM_OCM_PWM1);
    timer_enable_oc_preload(TIM5, TIM_OC3);
    timer_set_oc_value(TIM5, TIM_OC3, 0);
    timer_enable_oc_output(TIM5, TIM_OC3);
    timer_generate_event(TIM5, TIM_EGR_UG);
    timer_enable_counter(TIM5);

    rcc_periph_clock_enable(RCC_TIM4);
    rcc_periph_reset_pulse(RST_TIM4);
    timer_set_mode(TIM4, TIM_CR1_CKD_CK_INT, TIM_CR1_CMS_EDGE, TIM_CR1_DIR_UP);
    timer_set_prescaler(TIM4, 0);
    timer_set_period(TIM4, AO_TIM4_PACE_TICKS - 1u);

    rcc_periph_clock_enable(RCC_DMA1);
    dma_stream_reset(DMA1, DMA_STREAM6);
    dma_channel_select(DMA1, DMA_STREAM6, DMA_SxCR_CHSEL_2);
    dma_set_priority(DMA1, DMA_STREAM6, DMA_SxCR_PL_LOW);
    dma_set_transfer_mode(DMA1, DMA_STREAM6, DMA_SxCR_DIR_MEM_TO_PERIPHERAL);
    dma_set_memory_size(DMA1, DMA_STREAM6, DMA_SxCR_MSIZE_32BIT);
    dma_set_peripheral_size(DMA1, DMA_STREAM6, DMA_SxCR_PSIZE_32BIT);
    dma_enable_memory_increment_mode(DMA1, DMA_STREAM6);
    dma_enable_circular_mode(DMA1, DMA_STREAM6);
    dma_set_peripheral_address(DMA1, DMA_STREAM6, (uint32_t)&TIM5_CCR3);
    dma_set_memory_address(DMA1, DMA_STREAM6, (uint32_t)dither_3a);
    dma_set_number_of_data(DMA1, DMA_STREAM6, AO_DITHER_LEN);
    dma_enable_stream(DMA1, DMA_STREAM6);
    timer_enable_irq(TIM4, TIM_DIER_UDE);
    timer_enable_counter(TIM4);
    tim5_running = true;
}

static void tim5_stop(void) {
    timer_disable_irq(TIM4, TIM_DIER_UDE);
    dma_disable_stream(DMA1, DMA_STREAM6);
    timer_disable_counter(TIM4);
    rcc_periph_clock_disable(RCC_TIM4);
    timer_disable_counter(TIM5);
    rcc_periph_clock_disable(RCC_TIM5);
    tim5_running = false;
}

// --- Pins ---

static void pin_mode(analog_out_ch_t ch, bool af) {
    uint32_t const port = (ch == ANALOG_OUT_3A) ? GPIOA : AO_HW[ch].port;
    uint16_t const pin = (ch == ANALOG_OUT_3A) ? GPIO2 : AO_HW[ch].pin;
    if (af) {
        gpio_set_af(port, (ch == ANALOG_OUT_3A) ? GPIO_AF2 : GPIO_AF1, pin);
        gpio_set_output_options(port, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, pin);
        gpio_mode_setup(port, GPIO_MODE_AF, GPIO_PUPD_NONE, pin);
    } else {
        gpio_clear(port, pin);
        gpio_set_output_options(port, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, pin);
        gpio_mode_setup(port, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, pin);
    }
}

static bool any_tim1_channel(void) {
    return ch_enabled[ANALOG_OUT_1A] || ch_enabled[ANALOG_OUT_2A] || ch_enabled[ANALOG_OUT_2B];
}

void analog_out_enable(analog_out_ch_t ch, bool enable) {
    if (ch >= ANALOG_OUT_NUM || ch_enabled[ch] == enable || audio_running) {
        return;
    }
    if (ch == ANALOG_OUT_3A) {
        if (enable) {
            tim5_start();
            target_level[ch] = 0.0f;
            pin_mode(ch, true);
            ch_enabled[ch] = true;
        } else {
            ch_enabled[ch] = false;
            pin_mode(ch, false);
            tim5_stop();
        }
        return;
    }
    if (enable) {
        if (!tim1_running) {
            tim1_start(dither_buf, AO_DITHER_LEN * AO_BURST_REGS, AO_CV_RCR, false);
        }
        target_level[ch] = 0.0f;
        fill_channel(ch, 0.0f);
        timer_enable_oc_output(TIM1, AO_HW[ch].oc_n);
        pin_mode(ch, true);
        ch_enabled[ch] = true;
        return;
    }
    ch_enabled[ch] = false;
    pin_mode(ch, false);
    if (tim1_running) {
        timer_disable_oc_output(TIM1, AO_HW[ch].oc_n);
        fill_channel(ch, 0.0f);
        if (!any_tim1_channel()) {
            tim1_stop();
        }
    }
}

void analog_out_disable_all(void) {
    analog_out_audio_stop();
    for (uint32_t i = 0; i < ANALOG_OUT_NUM; i++) {
        analog_out_enable((analog_out_ch_t)i, false);
    }
}

void analog_out_set(analog_out_ch_t ch, float level) {
    if (ch < ANALOG_OUT_NUM) {
        target_level[ch] = level;
    }
}

void analog_out_update(uint32_t now_ms) {
    if (audio_running || now_ms == last_update_ms) {
        return;
    }
    last_update_ms = now_ms;
    for (uint32_t i = 0; i < ANALOG_OUT_NUM; i++) {
        if (ch_enabled[i]) {
            fill_channel((analog_out_ch_t)i, level_to_duty(target_level[i]));
        }
    }
}

// --- Audio streaming ---

float analog_out_audio_sample_rate(void) {
    float const burst_hz = (float)rcc_get_timer_clk_freq(TIM1) / (float)(AO_PWM_PERIOD_TICKS * (AO_AUDIO_RCR + 1u));
    return burst_hz / (float)AO_AUDIO_EXPAND;
}

/** Expand one sample into AO_AUDIO_EXPAND dithered CCR values (fixed-point error feedback). */
static AO_HOT void expand_sample(volatile uint32_t *dst, float level, uint32_t *acc) {
    uint32_t const dq = (uint32_t)(level_to_duty(level) * 256.0f);
    uint32_t const base = dq >> 8;
    uint32_t const frac = dq & 0xFFu;
    uint32_t a = *acc;
    for (uint32_t i = 0; i < AO_AUDIO_EXPAND; i++) {
        a += frac;
        uint32_t const carry = a >> 8;
        a &= 0xFFu;
        dst[i * AO_BURST_REGS] = base + carry;
    }
    *acc = a;
}

static AO_HOT void audio_render_half(uint32_t half) {
    float s[3][AO_AUDIO_SAMPLES_PER_HALF];
    if (audio_fn != 0) {
        audio_fn(s[0], s[1], s[2], AO_AUDIO_SAMPLES_PER_HALF);
    } else {
        for (uint32_t i = 0; i < AO_AUDIO_SAMPLES_PER_HALF; i++) {
            s[0][i] = s[1][i] = s[2][i] = 0.0f;
        }
    }
    static const analog_out_ch_t chs[3] = {ANALOG_OUT_1A, ANALOG_OUT_2A, ANALOG_OUT_2B};
    volatile uint32_t *base = &audio_buf[half * AO_AUDIO_HALF_WORDS];
    for (uint32_t k = 0; k < AO_AUDIO_SAMPLES_PER_HALF; k++) {
        volatile uint32_t *dst = base + k * AO_AUDIO_EXPAND * AO_BURST_REGS;
        for (uint32_t c = 0; c < 3u; c++) {
            expand_sample(dst + AO_HW[chs[c]].burst_slot, s[c][k], &audio_acc[c]);
        }
    }
}

AO_HOT void dma2_stream5_isr(void) {
    if (dma_get_interrupt_flag(DMA2, DMA_STREAM5, DMA_HTIF)) {
        dma_clear_interrupt_flags(DMA2, DMA_STREAM5, DMA_HTIF);
        audio_render_half(0u); // First half was just played -> refill it
    }
    if (dma_get_interrupt_flag(DMA2, DMA_STREAM5, DMA_TCIF)) {
        dma_clear_interrupt_flags(DMA2, DMA_STREAM5, DMA_TCIF);
        audio_render_half(1u);
    }
}

void analog_out_audio_start(analog_out_audio_fn fn) {
    analog_out_disable_all();
    audio_fn = fn;
    for (uint32_t i = 0; i < 3u; i++) {
        audio_acc[i] = 0u;
    }
    tim1_start(audio_buf, 2u * AO_AUDIO_HALF_WORDS, AO_AUDIO_RCR, true);
    audio_running = true;
    for (uint32_t i = 0; i < 3u; i++) {
        timer_enable_oc_output(TIM1, AO_HW[i].oc_n);
        pin_mode((analog_out_ch_t)i, true);
        ch_enabled[i] = true;
    }
}

void analog_out_audio_stop(void) {
    if (!audio_running) {
        return;
    }
    audio_running = false;
    tim1_stop();
    audio_fn = 0;
    for (uint32_t i = 0; i < 3u; i++) {
        ch_enabled[i] = false;
        pin_mode((analog_out_ch_t)i, false);
    }
}
