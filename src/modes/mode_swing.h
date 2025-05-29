#ifndef MODE_SWING_H
#define MODE_SWING_H

#include <stdint.h>

// NUM_SWING_PROFILES è usato da persistence.c e main.c per validare gli indici.
#define NUM_SWING_PROFILES 8 

// Le dichiarazioni di mode_swing_init, mode_swing_update, mode_swing_reset
// si trovano in modes.h come parte dell'interfaccia comune delle modalità.

// Funzioni specifiche per la modalità Swing per ottenere/impostare i suoi stati unici.
void mode_swing_set_profile_indices(uint8_t profile_index_a, uint8_t profile_index_b);
void mode_swing_get_profile_indices(uint8_t *p_profile_index_a, uint8_t *p_profile_index_b);

#endif // MODE_SWING_H
