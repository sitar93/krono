#pragma once

#include <stdint.h>
#include "modes.h"
#include "drivers/io.h"

#define MODE_RHYTHM_NUM_OUTPUTS 10

extern const jack_output_t mode_rhythm_jacks[MODE_RHYTHM_NUM_OUTPUTS];

uint16_t mode_rhythm_base_pattern(calculation_mode_t calc, int out_idx);
