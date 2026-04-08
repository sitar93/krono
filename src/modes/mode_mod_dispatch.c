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
        default:
            break;
    }
}
