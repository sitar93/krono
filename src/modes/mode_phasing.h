#ifndef MODE_PHASING_H
#define MODE_PHASING_H

#include "modes.h"
#include <stdint.h>
#include <stdbool.h>

// Functions are declared in modes.h
// void mode_phasing_init(void);
// void mode_phasing_update(const mode_context_t* context);
// void mode_phasing_reset(void);

/** Current Group B deviation level (0..2). */
uint8_t mode_phasing_get_delta_level(void);

#endif // MODE_PHASING_H
