#include "aux_led_mode_status.h"

#include "modes/mode_chaos.h"
#include "modes/mode_fixed.h"
#include "modes/mode_phasing.h"
#include "modes/mode_rhythm_shared.h"
#include "modes/mode_swing.h"

// --- Helpers ---

static void set_none(aux_led_status_t *out) {
    out->kind = AUX_LED_STATUS_NONE;
    out->level = 0;
    out->count = 0;
}

/** Steady on while `active`: freeze / stop, or bank B selected. */
static void set_on_if(aux_led_status_t *out, bool active) {
    out->kind = active ? AUX_LED_STATUS_ON : AUX_LED_STATUS_NONE;
}

/**
 * Continuous-mutation mode: always breathe (also at 0, as its normal state); peak follows the amount in [0, max].
 */
static void set_breathe_amount(aux_led_status_t *out, uint32_t value, uint32_t max) {
    out->kind = AUX_LED_STATUS_BREATHE;
    if (max == 0u) {
        out->level = 0;
        return;
    }
    if (value > max) {
        value = max;
    }
    out->level = (uint16_t)((value * 1000u) / max);
}

static void set_count(aux_led_status_t *out, uint8_t count) {
    out->kind = AUX_LED_STATUS_COUNT;
    out->count = count;
}

// --- Mapping ---

void aux_led_mode_status_get(operational_mode_t mode, calculation_mode_t calc_mode, aux_led_status_t *out) {
    set_none(out);

    switch (mode) {
    // ALPHA: MOD toggles A/B swap -> steady on while group B set (swapped)
    case MODE_DEFAULT:
    case MODE_EUCLIDEAN:
    case MODE_MUSICAL:
    case MODE_PROBABILISTIC:
    case MODE_SEQUENTIAL:
    case MODE_POLYRHYTHM:
    case MODE_LOGIC:
        set_on_if(out, calc_mode == CALC_MODE_SWAPPED);
        break;

    case MODE_SWING: { // Profile index cycles 0..7 -> count 1..8
        uint8_t idx_a = 0;
        uint8_t idx_b = 0;
        mode_swing_get_profile_indices(&idx_a, &idx_b);
        set_count(out, (uint8_t)(idx_a + 1u));
        break;
    }
    case MODE_PHASING: // Deviation level 0..2 -> count 1..3
        set_count(out, (uint8_t)(mode_phasing_get_delta_level() + 1u));
        break;

    case MODE_CHAOS: { // Divisor decreases from default (dark) to min (strongest breathing)
        uint32_t div = mode_chaos_get_divisor();
        uint32_t span = CHAOS_DIVISOR_DEFAULT - CHAOS_DIVISOR_MIN;
        set_breathe_amount(out, (div < CHAOS_DIVISOR_DEFAULT) ? (CHAOS_DIVISOR_DEFAULT - div) : 0u, span);
        break;
    }

    // BETA
    case MODE_FIXED: // Bank 0..9 -> count 1..10
        set_count(out, (uint8_t)(mode_fixed_get_bank() + 1u));
        break;
    case MODE_DRIFT: {
        bool active = false;
        bool ramp = false;
        uint8_t prob = 0;
        mode_drift_get_state(&active, &prob, &ramp);
        set_breathe_amount(out, active ? prob : 0u, 100u);
        break;
    }
    case MODE_FILL: {
        uint8_t density = 0;
        bool ramp = false;
        mode_fill_get_state(&density, &ramp);
        set_breathe_amount(out, density, 50u);
        break;
    }
    case MODE_SKIP: {
        bool active = false;
        bool ramp = false;
        uint8_t prob = 0;
        mode_skip_get_state(&active, &prob, &ramp);
        set_breathe_amount(out, active ? prob : 0u, 100u);
        break;
    }
    case MODE_STUTTER: { // Length 2/4/8 -> count 1/2/3
        bool active = false;
        bool ramp = false;
        uint8_t len = 2;
        uint16_t var_mask[MODE_RHYTHM_NUM_OUTPUTS];
        mode_stutter_get_state(&active, &len, &ramp, var_mask);
        set_count(out, (len >= 8u) ? 3u : ((len >= 4u) ? 2u : 1u));
        break;
    }
    case MODE_MORPH: {
        bool frozen = false;
        uint32_t gen = 0;
        uint16_t morphed[MODE_RHYTHM_NUM_OUTPUTS];
        mode_morph_get_state(&frozen, &gen, morphed);
        if (frozen) {
            set_on_if(out, true); // Generation stopped
        } else {
            set_breathe_amount(out, 1u, 1u); // Continuous morph stream
        }
        break;
    }
    case MODE_MUTE: {
        uint16_t mask = 0;
        uint8_t count = 0;
        bool ramp = false;
        uint16_t var_mask[MODE_RHYTHM_NUM_OUTPUTS];
        mode_mute_get_state(&mask, &count, &ramp, var_mask);
        set_breathe_amount(out, count, MODE_RHYTHM_NUM_OUTPUTS);
        break;
    }
    case MODE_DENSITY: {
        uint8_t pct = 0;
        bool ramp = false;
        mode_density_get_state(&pct, &ramp);
        set_breathe_amount(out, pct, 200u);
        break;
    }
    case MODE_SONG: { // New base loop scheduled by MOD: breathe until it takes over at the next cycle
        uint32_t seed = 0;
        bool pending = false;
        mode_song_get_state(&seed, &pending);
        if (pending) {
            set_breathe_amount(out, 1u, 1u);
        }
        break;
    }
    case MODE_ACCUMULATE: { // Frozen -> steady on; otherwise breathe with accumulation amount
        uint8_t active_count = 0;
        bool frozen = false;
        uint16_t active_mask = 0;
        uint8_t offsets[MODE_RHYTHM_NUM_OUTPUTS];
        uint16_t var_masks[MODE_RHYTHM_NUM_OUTPUTS];
        mode_accumulate_get_state(&active_count, &frozen, &active_mask, offsets, var_masks);
        if (frozen) {
            set_on_if(out, true);
        } else {
            set_breathe_amount(out, active_count, MODE_RHYTHM_NUM_OUTPUTS);
        }
        break;
    }

    // GAMMA
    case MODE_GAMMA_SEQUENTIAL_FREEZE: {
        bool frozen = false;
        uint8_t step = 0;
        mode_gamma_sequential_freeze_get_state(&frozen, &step);
        set_on_if(out, frozen);
        break;
    }
    case MODE_GAMMA_SEQUENTIAL_TRIP: { // Pattern 0..5 -> count 1..6
        uint8_t pattern = 0;
        uint8_t step = 0;
        mode_gamma_sequential_trip_get_state(&pattern, &step);
        set_count(out, (uint8_t)(pattern + 1u));
        break;
    }
    case MODE_GAMMA_PORTALS: {
        bool multiply = false;
        mode_gamma_portals_get_state(&multiply);
        set_on_if(out, multiply);
        break;
    }
    case MODE_GAMMA_COIN_TOSS: {
        bool invert = false;
        mode_gamma_coin_toss_get_state(&invert);
        set_on_if(out, invert);
        break;
    }
    case MODE_GAMMA_RATCHET: {
        bool dbl = false;
        mode_gamma_ratchet_get_state(&dbl);
        set_on_if(out, dbl);
        break;
    }
    case MODE_GAMMA_ANTI_RATCHET: {
        bool half = false;
        mode_gamma_anti_ratchet_get_state(&half);
        set_on_if(out, half);
        break;
    }
    case MODE_GAMMA_START_STOP: {
        bool muted = false;
        mode_gamma_start_stop_get_state(&muted);
        set_on_if(out, muted); // Outputs stopped
        break;
    }

    // DELTA
    case MODE_DELTA_TIDES: // Waveform index -> count 1..5
        set_count(out, (uint8_t)(mode_delta_get_param(0) + 1u));
        break;
    case MODE_DELTA_ORBIT: // Speed index -> count 1..4 (2 / 4 / 8 / 16 beats)
        set_count(out, (uint8_t)(mode_delta_get_param(1) + 1u));
        break;
    case MODE_DELTA_MONOLITH: // progression frozen -> steady on, evolving -> breathe
        if (mode_delta_get_param(5) == 1u) {
            set_on_if(out, true);
        } else {
            set_breathe_amount(out, 1u, 1u);
        }
        break;
    case MODE_DELTA_CHIMERA:  // bank 1..4
    case MODE_DELTA_WANDER:   // style 1..3
    case MODE_DELTA_BUTTERFLY:   // speed 1..4
    case MODE_DELTA_STATIC:   // colour 1..4
    case MODE_DELTA_PULSAR:   // kit 1..4
    case MODE_DELTA_ALLOY:    // ratio 1..4
    case MODE_DELTA_ASCENT:   // pattern 1..4
        set_count(out, (uint8_t)(mode_delta_get_param((uint8_t)(mode - MODE_DELTA_TIDES)) + 1u));
        break;

    // Event-only modes (RESET, FIRE, BOUNCE): no persistent state to show
    default:
        break;
    }
}
