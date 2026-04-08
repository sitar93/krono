#include "mode_fixed.h"
#include "../drivers/io.h"
#include "../main_constants.h"
#include "../variables.h"

#define NUM_FIXED_BANKS 10

// Output mapping: 2=Kick, 3=Snare, 4=Clap, 5=Open HH, 6=Closed HH
// A and B are slightly different for variation
static const uint16_t patterns[NUM_FIXED_BANKS][NUM_JACK_OUTPUTS] = {
    // Bank 0: Basic Techno
    [0] = {
        [JACK_OUT_2A] = 0b1000100010001000, [JACK_OUT_2B] = 0b0000100010001001,
        [JACK_OUT_3A] = 0b0010001000100010, [JACK_OUT_3B] = 0b0000001000100010,
        [JACK_OUT_4A] = 0b0010001000100010, [JACK_OUT_4B] = 0b0010000000000010,
        [JACK_OUT_5A] = 0b1010101010101010, [JACK_OUT_5B] = 0b0010101010101010,
        [JACK_OUT_6A] = 0b1111111111111111, [JACK_OUT_6B] = 0b1111111101111111,
        [JACK_OUT_1A] = 0, [JACK_OUT_1B] = 0, [JACK_OUT_UNUSED_PB2] = 0, [JACK_OUT_UNUSED_PB3] = 0,
        [JACK_OUT_UNUSED_PB4] = 0, [JACK_OUT_UNUSED_PB7] = 0, [JACK_OUT_UNUSED_PB11] = 0,
        [JACK_OUT_STATUS_LED_PA15] = 0, [JACK_OUT_AUX_LED_PA3] = 0
    },
    [1] = {
        [JACK_OUT_2A] = 0b1000000010000000, [JACK_OUT_2B] = 0b1000000000000001,
        [JACK_OUT_3A] = 0b0010000000100010, [JACK_OUT_3B] = 0b0000000000100010,
        [JACK_OUT_4A] = 0b0010000100000010, [JACK_OUT_4B] = 0b0010000100000000,
        [JACK_OUT_5A] = 0b1010101010101010, [JACK_OUT_5B] = 0b0010101010101010,
        [JACK_OUT_6A] = 0b1100110011001100, [JACK_OUT_6B] = 0b1100110011001000,
        [JACK_OUT_1A] = 0, [JACK_OUT_1B] = 0, [JACK_OUT_UNUSED_PB2] = 0, [JACK_OUT_UNUSED_PB3] = 0,
        [JACK_OUT_UNUSED_PB4] = 0, [JACK_OUT_UNUSED_PB7] = 0, [JACK_OUT_UNUSED_PB11] = 0,
        [JACK_OUT_STATUS_LED_PA15] = 0, [JACK_OUT_AUX_LED_PA3] = 0
    },
    [2] = {
        [JACK_OUT_2A] = 0b1111000011110000, [JACK_OUT_2B] = 0b1110000011110001,
        [JACK_OUT_3A] = 0b0000111100001111, [JACK_OUT_3B] = 0b0000011100001111,
        [JACK_OUT_4A] = 0b0011001100110011, [JACK_OUT_4B] = 0b0010001100110011,
        [JACK_OUT_5A] = 0b1100110011001100, [JACK_OUT_5B] = 0b0100110011001100,
        [JACK_OUT_6A] = 0b1111111111111111, [JACK_OUT_6B] = 0b1111111111111011,
        [JACK_OUT_1A] = 0, [JACK_OUT_1B] = 0, [JACK_OUT_UNUSED_PB2] = 0, [JACK_OUT_UNUSED_PB3] = 0,
        [JACK_OUT_UNUSED_PB4] = 0, [JACK_OUT_UNUSED_PB7] = 0, [JACK_OUT_UNUSED_PB11] = 0,
        [JACK_OUT_STATUS_LED_PA15] = 0, [JACK_OUT_AUX_LED_PA3] = 0
    },
    [3] = {
        [JACK_OUT_2A] = 0b0000111100001111, [JACK_OUT_2B] = 0b0000111000001111,
        [JACK_OUT_3A] = 0b0011001100110011, [JACK_OUT_3B] = 0b0010001100110011,
        [JACK_OUT_4A] = 0b1100110011001100, [JACK_OUT_4B] = 0b1000110011001100,
        [JACK_OUT_5A] = 0b1111111100000000, [JACK_OUT_5B] = 0b0111111100000000,
        [JACK_OUT_6A] = 0b0000000011111111, [JACK_OUT_6B] = 0b0000000010111111,
        [JACK_OUT_1A] = 0, [JACK_OUT_1B] = 0, [JACK_OUT_UNUSED_PB2] = 0, [JACK_OUT_UNUSED_PB3] = 0,
        [JACK_OUT_UNUSED_PB4] = 0, [JACK_OUT_UNUSED_PB7] = 0, [JACK_OUT_UNUSED_PB11] = 0,
        [JACK_OUT_STATUS_LED_PA15] = 0, [JACK_OUT_AUX_LED_PA3] = 0
    },
    [4] = {
        [JACK_OUT_2A] = 0b1000000000000000, [JACK_OUT_2B] = 0b0000000000000001,
        [JACK_OUT_3A] = 0b0000000010000000, [JACK_OUT_3B] = 0b0000000000000000,
        [JACK_OUT_4A] = 0b0000100000001000, [JACK_OUT_4B] = 0b0000000000001000,
        [JACK_OUT_5A] = 0b1010101010101010, [JACK_OUT_5B] = 0b0010101010101010,
        [JACK_OUT_6A] = 0b1111111111111111, [JACK_OUT_6B] = 0b1111111111111101,
        [JACK_OUT_1A] = 0, [JACK_OUT_1B] = 0, [JACK_OUT_UNUSED_PB2] = 0, [JACK_OUT_UNUSED_PB3] = 0,
        [JACK_OUT_UNUSED_PB4] = 0, [JACK_OUT_UNUSED_PB7] = 0, [JACK_OUT_UNUSED_PB11] = 0,
        [JACK_OUT_STATUS_LED_PA15] = 0, [JACK_OUT_AUX_LED_PA3] = 0
    },
    [5] = {
        [JACK_OUT_2A] = 0b1000000010000000, [JACK_OUT_2B] = 0b0000000010000001,
        [JACK_OUT_3A] = 0b0000000100000000, [JACK_OUT_3B] = 0b0000000000000000,
        [JACK_OUT_4A] = 0b0000010000010000, [JACK_OUT_4B] = 0b0000000000010000,
        [JACK_OUT_5A] = 0b0101010101010101, [JACK_OUT_5B] = 0b0001010101010101,
        [JACK_OUT_6A] = 0b1111000011110000, [JACK_OUT_6B] = 0b1110000011110000,
        [JACK_OUT_1A] = 0, [JACK_OUT_1B] = 0, [JACK_OUT_UNUSED_PB2] = 0, [JACK_OUT_UNUSED_PB3] = 0,
        [JACK_OUT_UNUSED_PB4] = 0, [JACK_OUT_UNUSED_PB7] = 0, [JACK_OUT_UNUSED_PB11] = 0,
        [JACK_OUT_STATUS_LED_PA15] = 0, [JACK_OUT_AUX_LED_PA3] = 0
    },
    [6] = {
        [JACK_OUT_2A] = 0b1000000000000000, [JACK_OUT_2B] = 0b0000000000000001,
        [JACK_OUT_3A] = 0b0000000000000000, [JACK_OUT_3B] = 0b0000000000000000,
        [JACK_OUT_4A] = 0b0000000000000000, [JACK_OUT_4B] = 0b0000000000000000,
        [JACK_OUT_5A] = 0b1010101010101010, [JACK_OUT_5B] = 0b0010101010101010,
        [JACK_OUT_6A] = 0b1111111111111111, [JACK_OUT_6B] = 0b1111111111111011,
        [JACK_OUT_1A] = 0, [JACK_OUT_1B] = 0, [JACK_OUT_UNUSED_PB2] = 0, [JACK_OUT_UNUSED_PB3] = 0,
        [JACK_OUT_UNUSED_PB4] = 0, [JACK_OUT_UNUSED_PB7] = 0, [JACK_OUT_UNUSED_PB11] = 0,
        [JACK_OUT_STATUS_LED_PA15] = 0, [JACK_OUT_AUX_LED_PA3] = 0
    },
    [7] = {
        [JACK_OUT_2A] = 0b1000000000001000, [JACK_OUT_2B] = 0b0000000000001001,
        [JACK_OUT_3A] = 0b0000000000100000, [JACK_OUT_3B] = 0b0000000000000000,
        [JACK_OUT_4A] = 0b0000000000000000, [JACK_OUT_4B] = 0b0000000000000000,
        [JACK_OUT_5A] = 0b0101010101010101, [JACK_OUT_5B] = 0b0001010101010101,
        [JACK_OUT_6A] = 0b1100110011001100, [JACK_OUT_6B] = 0b1100110011001000,
        [JACK_OUT_1A] = 0, [JACK_OUT_1B] = 0, [JACK_OUT_UNUSED_PB2] = 0, [JACK_OUT_UNUSED_PB3] = 0,
        [JACK_OUT_UNUSED_PB4] = 0, [JACK_OUT_UNUSED_PB7] = 0, [JACK_OUT_UNUSED_PB11] = 0,
        [JACK_OUT_STATUS_LED_PA15] = 0, [JACK_OUT_AUX_LED_PA3] = 0
    },
    [8] = {
        [JACK_OUT_2A] = 0b1001000100010000, [JACK_OUT_2B] = 0b1000000100010001,
        [JACK_OUT_3A] = 0b0010001000100010, [JACK_OUT_3B] = 0b0000001000100010,
        [JACK_OUT_4A] = 0b0001000100010001, [JACK_OUT_4B] = 0b0000000100010001,
        [JACK_OUT_5A] = 0b1010101010101010, [JACK_OUT_5B] = 0b0010101010101010,
        [JACK_OUT_6A] = 0b1111111111111111, [JACK_OUT_6B] = 0b1111111111111101,
        [JACK_OUT_1A] = 0, [JACK_OUT_1B] = 0, [JACK_OUT_UNUSED_PB2] = 0, [JACK_OUT_UNUSED_PB3] = 0,
        [JACK_OUT_UNUSED_PB4] = 0, [JACK_OUT_UNUSED_PB7] = 0, [JACK_OUT_UNUSED_PB11] = 0,
        [JACK_OUT_STATUS_LED_PA15] = 0, [JACK_OUT_AUX_LED_PA3] = 0
    },
    [9] = {
        [JACK_OUT_2A] = 0b1000100001000100, [JACK_OUT_2B] = 0b0000100001000101,
        [JACK_OUT_3A] = 0b0010001000100010, [JACK_OUT_3B] = 0b0000001000100010,
        [JACK_OUT_4A] = 0b0001010001010001, [JACK_OUT_4B] = 0b0000010001010001,
        [JACK_OUT_5A] = 0b0101010101010101, [JACK_OUT_5B] = 0b0001010101010101,
        [JACK_OUT_6A] = 0b1100110011001100, [JACK_OUT_6B] = 0b1000110011001100,
        [JACK_OUT_1A] = 0, [JACK_OUT_1B] = 0, [JACK_OUT_UNUSED_PB2] = 0, [JACK_OUT_UNUSED_PB3] = 0,
        [JACK_OUT_UNUSED_PB4] = 0, [JACK_OUT_UNUSED_PB7] = 0, [JACK_OUT_UNUSED_PB11] = 0,
        [JACK_OUT_STATUS_LED_PA15] = 0, [JACK_OUT_AUX_LED_PA3] = 0
    }
};

static uint8_t current_step = 0;
static uint8_t current_bank = 0;
static bool bank_change_pending = false;
static uint8_t pending_bank = 0;
static uint32_t next_step_time = 0;

void mode_fixed_init(void) {
    mode_fixed_reset();
    next_step_time = 0;
}

void mode_fixed_update(const mode_context_t* context) {
    uint32_t now = context->current_time_ms;
    uint32_t step_interval = context->current_tempo_interval_ms / 4;
    if (step_interval < 5) step_interval = 5;

    if (next_step_time == 0) next_step_time = now;

    if (now >= next_step_time) {
        next_step_time += step_interval;
        if (next_step_time < now) next_step_time = now + step_interval;

        for (int i = 0; i < NUM_JACK_OUTPUTS; i++) {
            uint16_t pattern = patterns[current_bank][i];
            if ((pattern >> current_step) & 1) {
                set_output_high_for_duration((jack_output_t)i, DEFAULT_PULSE_DURATION_MS);
            }
        }

        current_step++;
        if (current_step >= 16) {
            current_step = 0;
            if (bank_change_pending) {
                current_bank = pending_bank;
                bank_change_pending = false;
            }
        }
    }
}

void mode_fixed_reset(void) {
    current_step = 0;
    current_bank = 0;
    bank_change_pending = false;
    pending_bank = 0;

    for (int i = JACK_OUT_2A; i <= JACK_OUT_6B; i++) {
        set_output((jack_output_t)i, false);
    }
}

void mode_fixed_set_bank(uint8_t bank) {
    if (bank < NUM_FIXED_BANKS) current_bank = bank;
}

uint8_t mode_fixed_get_bank(void) { return current_bank; }

void mode_fixed_reset_step(void) { current_step = 0; }

void mode_fixed_set_bank_pending(uint8_t bank) {
    if (bank < NUM_FIXED_BANKS) {
        pending_bank = bank;
        bank_change_pending = true;
    }
}

uint8_t mode_fixed_get_bank_pending(void) { return pending_bank; }

bool mode_fixed_is_bank_change_pending(void) { return bank_change_pending; }

void mode_fixed_apply_bank_change(void) {
    if (bank_change_pending) {
        current_bank = pending_bank;
        bank_change_pending = false;
    }
}
