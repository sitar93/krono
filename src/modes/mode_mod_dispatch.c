#include "modes.h"

void mode_dispatch_mod_press(operational_mode_t op, mod_press_event_t ev, uint32_t ts_ms) {
    switch (op) {
        case MODE_DRIFT:
            mode_drift_on_mod_press(ev, ts_ms);
            break;
        case MODE_FILL:
            mode_fill_on_mod_press(ev, ts_ms);
            break;
        case MODE_SKIP:
            mode_skip_on_mod_press(ev, ts_ms);
            break;
        case MODE_STUTTER:
            mode_stutter_on_mod_press(ev, ts_ms);
            break;
        case MODE_MORPH:
            mode_morph_on_mod_press(ev, ts_ms);
            break;
        case MODE_MUTE:
            mode_mute_on_mod_press(ev, ts_ms);
            break;
        case MODE_DENSITY:
            mode_density_on_mod_press(ev, ts_ms);
            break;
        case MODE_SONG:
            mode_song_on_mod_press(ev, ts_ms);
            break;
        case MODE_ACCUMULATE:
            mode_accumulate_on_mod_press(ev, ts_ms);
            break;
        case MODE_GAMMA_SEQUENTIAL_RESET:
            mode_gamma_sequential_reset_on_mod_press(ev, ts_ms);
            break;
        case MODE_GAMMA_SEQUENTIAL_FREEZE:
            mode_gamma_sequential_freeze_on_mod_press(ev, ts_ms);
            break;
        case MODE_GAMMA_SEQUENTIAL_TRIP:
            mode_gamma_sequential_trip_on_mod_press(ev, ts_ms);
            break;
        case MODE_GAMMA_SEQUENTIAL_FIRE:
            mode_gamma_sequential_fire_on_mod_press(ev, ts_ms);
            break;
        case MODE_GAMMA_SEQUENTIAL_BOUNCE:
            mode_gamma_sequential_bounce_on_mod_press(ev, ts_ms);
            break;
        case MODE_GAMMA_PORTALS:
            mode_gamma_portals_on_mod_press(ev, ts_ms);
            break;
        case MODE_GAMMA_COIN_TOSS:
            mode_gamma_coin_toss_on_mod_press(ev, ts_ms);
            break;
        case MODE_GAMMA_RATCHET:
            mode_gamma_ratchet_on_mod_press(ev, ts_ms);
            break;
        case MODE_GAMMA_ANTI_RATCHET:
            mode_gamma_anti_ratchet_on_mod_press(ev, ts_ms);
            break;
        case MODE_GAMMA_START_STOP:
            mode_gamma_start_stop_on_mod_press(ev, ts_ms);
            break;
        default:
            break;
    }
}
