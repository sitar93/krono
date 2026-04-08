#include "mode_rhythm_shared.h"

const jack_output_t mode_rhythm_jacks[MODE_RHYTHM_NUM_OUTPUTS] = {
    JACK_OUT_2A, JACK_OUT_2B, JACK_OUT_3A, JACK_OUT_3B,
    JACK_OUT_4A, JACK_OUT_4B, JACK_OUT_5A, JACK_OUT_5B,
    JACK_OUT_6A, JACK_OUT_6B,
};

/* Same 10 tracks as mode_fixed banks 0 and 1 (NORMAL / SWAPPED). */
static const uint16_t g_rhythm_base[2][MODE_RHYTHM_NUM_OUTPUTS] = {
    {
        0b1000100010001000, 0b0000100010001001,
        0b0010001000100010, 0b0000001000100010,
        0b0010001000100010, 0b0010000000000010,
        0b1010101010101010, 0b0010101010101010,
        0b1111111111111111, 0b1111111101111111,
    },
    {
        0b1000000010000000, 0b1000000000000001,
        0b0010000000100010, 0b0000000000100010,
        0b0010000100000010, 0b0010000100000000,
        0b1010101010101010, 0b0010101010101010,
        0b1100110011001100, 0b1100110011001000,
    },
};

uint16_t mode_rhythm_base_pattern(calculation_mode_t calc, int out_idx) {
    if (out_idx < 0 || out_idx >= MODE_RHYTHM_NUM_OUTPUTS) {
        return 0;
    }
    int bank = (calc == CALC_MODE_SWAPPED) ? 1 : 0;
    return g_rhythm_base[bank][out_idx];
}
