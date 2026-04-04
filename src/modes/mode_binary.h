#ifndef MODE_BINARY_H
#define MODE_BINARY_H

#include <stdint.h>
#include <stdbool.h>
#include "modes/modes.h"

#define NUM_BINARY_SUBMODES 5

// Functions are declared in modes.h
// void mode_binary_init(void);
// void mode_binary_update(const mode_context_t* context);
// void mode_binary_reset(void);

// Additional functions for persistence/interaction
void mode_binary_set_bank(uint8_t bank);
void mode_binary_set_sequence(uint8_t seq);
uint8_t mode_binary_get_bank(void);
uint8_t mode_binary_get_sequence(void);
void mode_binary_reset_step(void);

#endif // MODE_BINARY_H
