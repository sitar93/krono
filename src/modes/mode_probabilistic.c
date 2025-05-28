#include "mode_probabilistic.h"
#include "drivers/io.h"
#include "main_constants.h"
#include "util/delay.h" // For millis()
#include <stdlib.h> // For rand() and srand()
#include <libopencm3/stm32/rcc.h> // For seeding RNG
#include <libopencm3/stm32/rtc.h> // For seeding RNG

// --- Module Constants ---
#define NUM_PROB_OUTPUTS 5 // Outputs 2A/2B to 6A/6B

// Probabilities for Group A (Linear)
static const float PROB_A[NUM_PROB_OUTPUTS] = {0.1f, 0.2f, 0.3f, 0.4f, 0.5f};
// Probabilities for Group B (Exponential Decay)
static const float PROB_B[NUM_PROB_OUTPUTS] = {0.5f, 0.25f, 0.125f, 0.0625f, 0.03125f};

// Map abstract groups A/B to physical jack outputs
static const jack_output_t group_a_jacks[NUM_PROB_OUTPUTS] = {
    JACK_OUT_2A, JACK_OUT_3A, JACK_OUT_4A,
    JACK_OUT_5A, JACK_OUT_6A
};
static const jack_output_t group_b_jacks[NUM_PROB_OUTPUTS] = {
    JACK_OUT_2B, JACK_OUT_3B, JACK_OUT_4B,
    JACK_OUT_5B, JACK_OUT_6B
};

// --- Module State ---
// None needed for this mode

// --- Helper Functions ---
static void seed_rng(void) {
    uint32_t seed = 0;
    // Use RTC counter if available and running
    // Note: RTC setup is not shown here, assuming it might be elsewhere
    // if ((RCC_BDCR & RCC_BDCR_LSERDY) && (RTC_CRL & RTC_CRL_RSF)) {
    //     seed = rtc_get_counter_val();
    // } else {
    //     seed = 0xDEADBEEF; // Fallback seed if RTC not ready/configured
    // }
    seed = 0xDEADBEEF ^ millis(); // Use SysTick as a simple seed source
    srand(seed);
}

// --- Public Function Implementations ---

void mode_probabilistic_init(void) {
    seed_rng();
    mode_probabilistic_reset();
}

void mode_probabilistic_update(const mode_context_t* context) {
    if (!context->f1_rising_edge) { // Use renamed field
        return;
    }

    const float* current_probs_a = (context->calc_mode == CALC_MODE_NORMAL) ? PROB_A : PROB_B; // Use renamed field
    const float* current_probs_b = (context->calc_mode == CALC_MODE_NORMAL) ? PROB_B : PROB_A; // Use renamed field

    // Group A triggers
    for (int i = 0; i < NUM_PROB_OUTPUTS; i++) {
        // Generate a random float between 0.0 and 1.0
        float random_val = (float)rand() / (float)RAND_MAX;
        if (random_val < current_probs_a[i]) {
            set_output_high_for_duration(group_a_jacks[i], DEFAULT_PULSE_DURATION_MS); // Use central constant
        }
    }

    // Group B triggers
    for (int i = 0; i < NUM_PROB_OUTPUTS; i++) {
        float random_val = (float)rand() / (float)RAND_MAX;
        if (random_val < current_probs_b[i]) {
            set_output_high_for_duration(group_b_jacks[i], DEFAULT_PULSE_DURATION_MS); // Use central constant
        }
    }
}

void mode_probabilistic_reset(void) {
    for (int i = 0; i < NUM_PROB_OUTPUTS; i++) {
        set_output(group_a_jacks[i], false);
        set_output(group_b_jacks[i], false);
    }
}
