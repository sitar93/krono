#include "tap.h"
#include "../main_constants.h"
#include "../util/delay.h"
#include <libopencm3/cm3/common.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/exti.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/syscfg.h>
#include <stdbool.h>
#include <stdint.h>

/* Cortex-M4 DWT cycle counter (sub-ms tap intervals). */
#define SCB_DEMCR   MMIO32(0xE000EDFCUL)
#define DWT_CTRL    MMIO32(0xE0001000UL)
#define DWT_CYCCNT  MMIO32(0xE0001004UL)

#define DEMCR_TRCENA (1UL << 24)
#define DWT_CYCCNTENA (1UL << 0)

static volatile uint32_t last_tap_time_ms = 0;
static volatile uint32_t last_accept_cycles = 0;
static volatile uint32_t tap_interval = 0;
static volatile bool tap_detected_flag = false;
static volatile bool first_tap_registered = false;

static void tap_dwt_enable(void) {
    SCB_DEMCR |= DEMCR_TRCENA;
    DWT_CYCCNT = 0;
    DWT_CTRL |= DWT_CYCCNTENA;
}

static uint32_t tap_cycles_now(void) {
    return DWT_CYCCNT;
}

/**
 * @brief Convert elapsed core cycles to milliseconds (rounded).
 */
static uint32_t tap_cycles_to_ms_rounded(uint32_t delta_cycles) {
    if (delta_cycles == 0) {
        return 0;
    }
    uint32_t half = KRONO_CPU_CYCLES_PER_MS / 2u;
    return (delta_cycles + half) / KRONO_CPU_CYCLES_PER_MS;
}

void tap_init(void) {
    rcc_periph_clock_enable(RCC_GPIOA);
    rcc_periph_clock_enable(RCC_SYSCFG);

    tap_dwt_enable();

    gpio_mode_setup(GPIOA, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, GPIO0);

    nvic_enable_irq(NVIC_EXTI0_IRQ);
    exti_select_source(EXTI0, GPIOA);
    exti_set_trigger(EXTI0, EXTI_TRIGGER_FALLING);
    exti_enable_request(EXTI0);

    last_tap_time_ms = 0;
    last_accept_cycles = 0;
    tap_interval = 0;
    tap_detected_flag = false;
    first_tap_registered = false;
}

void exti0_isr(void) {
    exti_reset_request(EXTI0);

    uint32_t now_ms = millis();
    uint32_t now_cyc = tap_cycles_now();
    uint32_t debounce_cycles = (uint32_t)DEBOUNCE_DELAY_MS * KRONO_CPU_CYCLES_PER_MS;

    if (first_tap_registered) {
        uint32_t dc = now_cyc - last_accept_cycles;
        if (dc < debounce_cycles) {
            return;
        }
    }

    if (!first_tap_registered) {
        last_tap_time_ms = now_ms;
        last_accept_cycles = now_cyc;
        first_tap_registered = true;
        tap_interval = 0;
        tap_detected_flag = true;
        return;
    }

    {
        uint32_t dc = now_cyc - last_accept_cycles;
        tap_interval = tap_cycles_to_ms_rounded(dc);
        last_tap_time_ms = now_ms;
        last_accept_cycles = now_cyc;
        tap_detected_flag = true;
    }
}

bool tap_detected(void) {
    if (tap_detected_flag) {
        tap_detected_flag = false;
        return true;
    }
    return false;
}

uint32_t tap_get_interval(void) {
    return tap_interval;
}

uint32_t tap_get_last_press_time_ms(void) {
    return last_tap_time_ms;
}

bool tap_is_button_pressed(void) {
    return (gpio_get(GPIOA, GPIO0) == 0);
}

bool tap_check_timeout(uint32_t current_time_ms) {
    if (first_tap_registered && (current_time_ms - last_tap_time_ms > TAP_TIMEOUT_MS)) {
        first_tap_registered = false;
        tap_interval = 0;
        return true;
    }
    return false;
}

void tap_abort_capture(void) {
    nvic_disable_irq(NVIC_EXTI0_IRQ);
    tap_detected_flag = false;
    tap_interval = 0;
    first_tap_registered = false;
    nvic_enable_irq(NVIC_EXTI0_IRQ);
}
