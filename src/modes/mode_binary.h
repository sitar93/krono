#ifndef MODE_BINARY_H
#define MODE_BINARY_H

#include <stdint.h>
#include <stdbool.h>
#include "modes/modes.h"

#define NUM_BINARY_BANKS 10

void mode_binary_init(void);
void mode_binary_update(const mode_context_t* context);
void mode_binary_reset(void);

void mode_binary_set_bank(uint8_t bank);
uint8_t mode_binary_get_bank(void);
void mode_binary_reset_step(void);

void mode_binary_set_bank_pending(uint8_t bank);
uint8_t mode_binary_get_bank_pending(void);
bool mode_binary_is_bank_change_pending(void);
void mode_binary_apply_bank_change(void);

#endif // MODE_BINARY_H
