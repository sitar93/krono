#include "mode_binary.h"
#include "../drivers/io.h"
#include "../main_constants.h"
#include "../variables.h"

#define NUM_BINARY_SUBMODES 5

// 10 outputs: 2A-6A and 2B-6B
// bank 0 = calc_mode NORMAL (A group), bank 1 = calc_mode SWAPPED
static const uint16_t patterns[NUM_BINARY_SUBMODES][2][NUM_JACK_OUTPUTS] = {
    // Mode 11: Techno
    [0] = {
        // Bank 0 (calc_mode = NORMAL): Group A = 2A-6A, Group B = 2B-6B
        [0] = {
            // Group A outputs (2A-6A)
            [JACK_OUT_2A] = 0b1000100010001000, // Kick four-on-the-floor
            [JACK_OUT_3A] = 0b1010101010101010, // Hi-hat 8th
            [JACK_OUT_4A] = 0b1111111111111111, // Hi-hat 16th
            [JACK_OUT_5A] = 0b0000100000001000, // Clap on 2 e 4
            [JACK_OUT_6A] = 0b0001000100010001, // Perc pattern
            // Group B outputs (2B-6B)
            [JACK_OUT_2B] = 0b0000100101000100, // Snare
            [JACK_OUT_3B] = 0b0101010101010101, // Hi-hat offset
            [JACK_OUT_4B] = 0b1111000011110000, // Hi-hat variant
            [JACK_OUT_5B] = 0b0000010000010000, // Clap variant
            [JACK_OUT_6B] = 0b0010001000100010, // Perc variant
            // Ignored outputs (1A, 1B, unused)
            [JACK_OUT_1A] = 0b0000000000000000,
            [JACK_OUT_1B] = 0b0000000000000000,
            [JACK_OUT_UNUSED_PB2] = 0b0000000000000000,
            [JACK_OUT_UNUSED_PB3] = 0b0000000000000000,
            [JACK_OUT_UNUSED_PB4] = 0b0000000000000000,
            [JACK_OUT_UNUSED_PB7] = 0b0000000000000000,
            [JACK_OUT_UNUSED_PB11] = 0b0000000000000000,
            [JACK_OUT_STATUS_LED_PA15] = 0b0000000000000000,
            [JACK_OUT_AUX_LED_PA3] = 0b0000000000000000
        },
        // Bank 1 (calc_mode = SWAPPED): Group A e B invertiti
        [1] = {
            // Group A outputs (2A-6A) - different pattern from Bank 0
            [JACK_OUT_2A] = 0b1001000100010000, // Kick industrial
            [JACK_OUT_3A] = 0b1011010101101010, // Hi-hat shuffle
            [JACK_OUT_4A] = 0b1100001100000100, // Perc burst
            [JACK_OUT_5A] = 0b0010100100100101, // Rimshot
            [JACK_OUT_6A] = 0b0010010010010010, // Perc 3
            // Group B outputs (2B-6B) - different pattern from Bank 0
            [JACK_OUT_2B] = 0b0100010001000100, // Offbeat perc
            [JACK_OUT_3B] = 0b0110101011010110, // Hi-hat shuffle variant
            [JACK_OUT_4B] = 0b0011000011000001, // Perc burst variant
            [JACK_OUT_5B] = 0b0101001001010010, // Rimshot variant
            [JACK_OUT_6B] = 0b0100100100100100, // Perc 4
            // Ignored outputs
            [JACK_OUT_1A] = 0b0000000000000000,
            [JACK_OUT_1B] = 0b0000000000000000,
            [JACK_OUT_UNUSED_PB2] = 0b0000000000000000,
            [JACK_OUT_UNUSED_PB3] = 0b0000000000000000,
            [JACK_OUT_UNUSED_PB4] = 0b0000000000000000,
            [JACK_OUT_UNUSED_PB7] = 0b0000000000000000,
            [JACK_OUT_UNUSED_PB11] = 0b0000000000000000,
            [JACK_OUT_STATUS_LED_PA15] = 0b0000000000000000,
            [JACK_OUT_AUX_LED_PA3] = 0b0000000000000000
        }
    }
};

static uint8_t current_step = 0;
static uint8_t current_bank = 0;
static uint8_t current_sequence = 0;
static bool pending_bank_swap = false;

void mode_binary_init(void) {
    mode_binary_reset();
}

void mode_binary_update(const mode_context_t* context) {
    // Only check bank/calc_mode mismatch on F1 rising edge to avoid constant checking
    // Also check sync_request to avoid swap during save
    if (context->f1_rising_edge && !context->sync_request) {
        uint8_t expected_bank = (context->calc_mode == CALC_MODE_NORMAL) ? 0 : 1;
        if (current_bank != expected_bank) {
            pending_bank_swap = true;
        }
    }

    if (!context->f1_rising_edge) {
        return;
    }

    if (pending_bank_swap) {
        if (current_step == 0 || current_step == 3 || current_step == 7 || current_step == 11 || current_step == 15) {
            current_bank = (current_bank == 0) ? 1 : 0;
            pending_bank_swap = false;
        }
    }

    uint8_t seq = current_sequence;
    uint8_t bank = current_bank;

    for (int i = 0; i < NUM_JACK_OUTPUTS; i++) {
        uint16_t pattern = patterns[seq][bank][i];
        uint8_t bit_pos = current_step;
        if ((pattern >> bit_pos) & 1) {
            set_output_high_for_duration((jack_output_t)i, DEFAULT_PULSE_DURATION_MS);
        }
    }

    current_step++;
    if (current_step >= 16) {
        current_step = 0;
    }
}

void mode_binary_reset(void) {
    current_step = 0;
    current_bank = 0;
    current_sequence = 0;

    // Turn off only outputs 2A-6A and 2B-6B (not 1A/1B which are handled by clock_manager)
    for (int i = JACK_OUT_2A; i <= JACK_OUT_6B; i++) {
        set_output((jack_output_t)i, false);
    }
}

void mode_binary_set_bank(uint8_t bank) {
    if (bank <= 1) {
        current_bank = bank;
    }
}

void mode_binary_set_sequence(uint8_t seq) {
    if (seq < NUM_BINARY_SUBMODES) {
        current_sequence = seq;
    }
}

uint8_t mode_binary_get_bank(void) {
    return current_bank;
}

uint8_t mode_binary_get_sequence(void) {
    return current_sequence;
}

void mode_binary_reset_step(void) {
    current_step = 0;
}