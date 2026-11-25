#ifndef AUX_LED_MODE_STATUS_H
#define AUX_LED_MODE_STATUS_H

#include "aux_led.h"
#include "modes/modes.h"

/**
 * Map the active mode's runtime state to what the Aux LED (under MOD) should show:
 * steady on for freeze / stop / bank B, breathing while a modulation is active, a count for enumerated values.
 */
void aux_led_mode_status_get(operational_mode_t mode, calculation_mode_t calc_mode, aux_led_status_t *out);

#endif
