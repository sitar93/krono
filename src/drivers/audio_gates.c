#include "audio_gates.h"

#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/timer.h>
#include <libopencm3/cm3/nvic.h>

typedef struct {
    uint32_t port;
    uint16_t pin;
} ag_pin_t;

static const ag_pin_t AG_PINS[AUDIO_GATES_NUM] = {
    {GPIOA, GPIO2},  // 3A
    {GPIOB, GPIO15}, // 4A
    {GPIOB, GPIO5},  // 5A
    {GPIOB, GPIO6},  // 6A
    {GPIOB, GPIO14}, // 1B
    {GPIOB, GPIO13}, // 2B
    {GPIOB, GPIO12}, // 3B
    {GPIOB, GPIO8},  // 4B
    {GPIOB, GPIO9},  // 5B
    {GPIOB, GPIO10}, // 6B
};

typedef struct {
    volatile uint8_t type;
    volatile uint8_t src_a;
    volatile uint8_t src_b;
    volatile bool enabled;
    volatile uint32_t inc;
    volatile uint32_t pw; // Square: high while phase < pw (0x80000000 = 50 %)
    uint32_t phase;
    uint32_t lfsr;
    uint8_t bit;
} ag_voice_t;

static ag_voice_t voices[AUDIO_GATES_NUM];
static bool running = false;

static uint32_t freq_to_inc(float f) {
    if (f <= 0.0f) {
        return 0u;
    }
    float const x = f / (float)AUDIO_GATES_RATE_HZ;
    if (x >= 0.5f) {
        return 0x80000000u;
    }
    return (uint32_t)(x * 4294967296.0f);
}

__attribute__((optimize("O2"))) void tim4_isr(void) {
    TIM_SR(TIM4) = ~TIM_SR_UIF; // Clear update flag (rc_w0)

    uint32_t bits = 0; // bit i = voice i output
    // Oscillators first, then logic (which reads other voices' bits)
    for (uint32_t i = 0; i < AUDIO_GATES_NUM; i++) {
        ag_voice_t *v = &voices[i];
        uint8_t const type = v->type;
        uint8_t bit = v->bit;
        if (type == AG_SQUARE) {
            v->phase += v->inc;
            bit = (v->phase < v->pw) ? 1u : 0u;
        } else if (type == AG_NOISE) {
            uint32_t const old = v->phase;
            v->phase = old + v->inc;
            if (v->phase < old) { // Wrap -> next noise bit (Galois LFSR, 32-bit)
                uint32_t l = v->lfsr;
                l = (l >> 1) ^ ((0u - (l & 1u)) & 0xA3000000u);
                v->lfsr = l;
                bit = (uint8_t)(l & 1u);
            }
        } else if (type == AG_HIGH) {
            bit = 1u;
        } else if (type == AG_OFF) {
            bit = 0u;
        }
        v->bit = bit;
        bits |= (uint32_t)bit << i;
    }
    for (uint32_t i = 0; i < AUDIO_GATES_NUM; i++) {
        ag_voice_t *v = &voices[i];
        uint8_t const type = v->type;
        if (type >= AG_XOR) {
            uint32_t const a = (bits >> v->src_a) & 1u;
            uint32_t const b = (bits >> v->src_b) & 1u;
            uint32_t const r = (type == AG_XOR) ? (a ^ b) : (type == AG_AND) ? (a & b) : (a | b);
            v->bit = (uint8_t)r;
            bits = (bits & ~(1u << i)) | (r << i);
        }
    }
    uint32_t set_a = 0, clr_a = 0, set_b = 0, clr_b = 0;
    for (uint32_t i = 0; i < AUDIO_GATES_NUM; i++) {
        bool const high = voices[i].enabled && ((bits >> i) & 1u);
        uint32_t const pin = AG_PINS[i].pin;
        if (AG_PINS[i].port == GPIOA) {
            if (high) {
                set_a |= pin;
            } else {
                clr_a |= pin;
            }
        } else {
            if (high) {
                set_b |= pin;
            } else {
                clr_b |= pin;
            }
        }
    }
    GPIO_BSRR(GPIOA) = set_a | (clr_a << 16);
    GPIO_BSRR(GPIOB) = set_b | (clr_b << 16);
}

void audio_gates_start(void) {
    for (uint32_t i = 0; i < AUDIO_GATES_NUM; i++) {
        voices[i].type = AG_OFF;
        voices[i].enabled = true;
        voices[i].inc = 0;
        voices[i].pw = 0x80000000u;
        voices[i].phase = 0;
        voices[i].lfsr = 0xACE1u + i * 7919u;
        voices[i].bit = 0;
    }
    rcc_periph_clock_enable(RCC_TIM4);
    rcc_periph_reset_pulse(RST_TIM4);
    timer_set_mode(TIM4, TIM_CR1_CKD_CK_INT, TIM_CR1_CMS_EDGE, TIM_CR1_DIR_UP);
    timer_set_prescaler(TIM4, 0);
    timer_set_period(TIM4, (rcc_get_timer_clk_freq(TIM4) / AUDIO_GATES_RATE_HZ) - 1u);
    timer_enable_irq(TIM4, TIM_DIER_UIE);
    nvic_enable_irq(NVIC_TIM4_IRQ);
    timer_enable_counter(TIM4);
    running = true;
}

void audio_gates_stop(void) {
    if (!running) {
        return;
    }
    timer_disable_counter(TIM4);
    timer_disable_irq(TIM4, TIM_DIER_UIE);
    nvic_disable_irq(NVIC_TIM4_IRQ);
    rcc_periph_clock_disable(RCC_TIM4);
    running = false;
    for (uint32_t i = 0; i < AUDIO_GATES_NUM; i++) {
        gpio_clear(AG_PINS[i].port, AG_PINS[i].pin);
    }
}

bool audio_gates_running(void) {
    return running;
}

void audio_gates_set(audio_gate_voice_t v, audio_gate_type_t type, float freq_hz, uint8_t src_a, uint8_t src_b) {
    if ((uint32_t)v >= AUDIO_GATES_NUM) {
        return;
    }
    voices[v].src_a = (uint8_t)(src_a % AUDIO_GATES_NUM);
    voices[v].src_b = (uint8_t)(src_b % AUDIO_GATES_NUM);
    voices[v].inc = freq_to_inc(freq_hz);
    voices[v].type = (uint8_t)type;
}

void audio_gates_set_pw(audio_gate_voice_t v, float width) {
    if ((uint32_t)v >= AUDIO_GATES_NUM) {
        return;
    }
    width = (width < 0.01f) ? 0.01f : ((width > 0.99f) ? 0.99f : width);
    voices[v].pw = (uint32_t)(width * 4294967296.0f);
}

void audio_gates_set_freq(audio_gate_voice_t v, float freq_hz) {
    if ((uint32_t)v < AUDIO_GATES_NUM) {
        voices[v].inc = freq_to_inc(freq_hz);
    }
}

void audio_gates_set_enabled(audio_gate_voice_t v, bool on) {
    if ((uint32_t)v < AUDIO_GATES_NUM) {
        voices[v].enabled = on;
    }
}
