#ifndef MODE_FIXED_H
#define MODE_FIXED_H

#include <stdint.h>
#include <stdbool.h>
#include "modes/modes.h"

#define NUM_FIXED_BANKS 10

void mode_fixed_init(void);
void mode_fixed_update(const mode_context_t* context);
void mode_fixed_reset(void);

void mode_fixed_set_bank(uint8_t bank);
uint8_t mode_fixed_get_bank(void);
void mode_fixed_reset_step(void);

void mode_fixed_set_bank_pending(uint8_t bank);
uint8_t mode_fixed_get_bank_pending(void);
bool mode_fixed_is_bank_change_pending(void);
void mode_fixed_apply_bank_change(void);

#endif // MODE_FIXED_H
