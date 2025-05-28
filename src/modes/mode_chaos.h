#ifndef MODE_CHAOS_H
#define MODE_CHAOS_H

#include "modes.h"
#include <stdbool.h>
#include <stdint.h>

// Divisor Constants (used by persistence and main)
#define CHAOS_DIVISOR_DEFAULT 1000
#define CHAOS_DIVISOR_STEP    50
#define CHAOS_DIVISOR_MIN     10

// Functions are declared in modes.h
// void mode_chaos_init(void);
// void mode_chaos_update(const mode_context_t *context);
// void mode_chaos_reset(void);

// Functions for persistence (Keep these specific declarations)
// Or better yet, move these into persistence.h or a dedicated config module if needed outside chaos mode?
// For now, keep them here as they are chaos-specific parameters.
uint32_t mode_chaos_get_divisor(void);
void mode_chaos_set_divisor(uint32_t divisor);

#endif // MODE_CHAOS_H
