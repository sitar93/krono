#ifndef MODE_STATE_H
#define MODE_STATE_H

#include "modes/modes.h"
#include "drivers/persistence.h"

void mode_state_validate(krono_state_t *state);
void mode_state_apply_runtime(operational_mode_t op_mode, const krono_state_t *state);
void mode_state_capture_for_save(operational_mode_t op_mode,
                                 const krono_state_t *current_state,
                                 krono_state_t *state_to_save);

#endif
