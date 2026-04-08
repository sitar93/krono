#include "plugin.hpp"
#include "krono_hw_engine.hpp"

#include <algorithm>
#include <array>

template<typename T>
static T kClamp(T v, T lo, T hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

namespace {

static constexpr uint32_t kFwMinInterval = 33;
static constexpr uint32_t kFwMaxInterval = 6000;
static constexpr uint32_t kFwMaxDiff = 3000;
static constexpr uint32_t kTapTimeoutMs = 10000;
static constexpr uint32_t kExtTimeoutMs = 5000;
static constexpr uint32_t kDefaultTempoIv = 60000 / 70;

static constexpr uint32_t kOpModeTapHoldMs = 1000;
static constexpr uint32_t kOpModeConfirmTimeoutMs = 10000;
static constexpr uint32_t kOpModeSaveTimeoutMs = 5000;
static constexpr uint32_t kPa1DebounceMs = 50;
static constexpr uint32_t kCalcSwapMaxMs = 500;
static constexpr uint32_t kCalcSwapCooldownMs = 100;

static constexpr uint32_t kLedBaseMs = 200;
static constexpr uint32_t kLedEndOffMs = 500;
static constexpr uint32_t kAuxPulseMs = 100;

static constexpr int kNumOpModes = (int)krono::OpMode::Count;

/// Mirrors firmware `status_led.c` (non-inverted logical brightness for Rack).
struct HwStatusLed {
    uint32_t last_blink_time = 0;
    bool led_state = false;
    int current_mode_for_led = 0;
    uint8_t blink_count = 0;

    bool override_active = false;
    bool override_fixed_state = false;
    float out_brightness = 0.f;

    static int blinksForMode(int modeIdx) {
        if (modeIdx < 0 || modeIdx >= kNumOpModes)
            return 1;
        return modeIdx + 1;
    }

    void set_led(bool on) {
        led_state = on;
        out_brightness = on ? 1.f : 0.f;
    }

    void reset_sequence(uint32_t now) {
        if (!override_active) {
            blink_count = 0;
            last_blink_time = now;
            set_led(false);
        }
    }

    void sync_mode(int modeIdx, uint32_t now) {
        if (modeIdx < 0 || modeIdx >= kNumOpModes)
            modeIdx = 0;
        if (modeIdx != current_mode_for_led && !override_active) {
            current_mode_for_led = modeIdx;
            reset_sequence(now);
        } else {
            current_mode_for_led = modeIdx;
        }
    }

    void set_override(bool active, bool fixed_state, uint32_t now) {
        const bool was = override_active;
        override_active = active;
        override_fixed_state = fixed_state;
        if (active && !was)
            set_led(fixed_state);
        else if (!active && was)
            reset_sequence(now);
        else if (active && was)
            set_led(fixed_state);
    }

    void update(uint32_t current_time_ms) {
        if (override_active) {
            set_led(override_fixed_state);
            return;
        }

        const uint8_t blinks = (uint8_t)blinksForMode(current_mode_for_led);
        uint32_t on_duration = kLedBaseMs;
        uint32_t off_duration = kLedBaseMs;
        const uint32_t sequence_pause = kLedEndOffMs;
        if (on_duration == 0)
            on_duration = 1;
        if (off_duration == 0)
            off_duration = 1;

        if (blink_count >= blinks) {
            if (current_time_ms - last_blink_time >= sequence_pause) {
                blink_count = 0;
                last_blink_time = current_time_ms;
                set_led(false);
            } else {
                if (led_state)
                    set_led(false);
                return;
            }
        }

        if (!led_state) {
            if (current_time_ms - last_blink_time >= off_duration) {
                set_led(true);
                last_blink_time = current_time_ms;
            }
        } else {
            if (current_time_ms - last_blink_time >= on_duration) {
                set_led(false);
                last_blink_time = current_time_ms;
                blink_count++;
            }
        }
    }
};

enum class OpSmState {
    Idle,
    TapHeldQualifying,
    TapQualifiedWaitingRelease,
    AwaitingModPressOrTimeout,
    AwaitingConfirmTap
};

enum class CalcSwapSmState { Idle, ModePressed };

} // namespace

struct Krono : Module {
    enum ParamIds {
        TAP_PARAM,
        MOD_PARAM,
        NUM_PARAMS
    };

    enum InputIds {
        CLOCK_INPUT,
        SWAP_INPUT,
        NUM_INPUTS
    };

    enum OutputIds {
        OUT_1A_OUTPUT,
        OUT_2A_OUTPUT,
        OUT_3A_OUTPUT,
        OUT_4A_OUTPUT,
        OUT_5A_OUTPUT,
        OUT_6A_OUTPUT,
        OUT_1B_OUTPUT,
        OUT_2B_OUTPUT,
        OUT_3B_OUTPUT,
        OUT_4B_OUTPUT,
        OUT_5B_OUTPUT,
        OUT_6B_OUTPUT,
        NUM_OUTPUTS
    };

    enum LightIds {
        STATUS_LED_LIGHT,
        AUX_LED_LIGHT,
        OUT_1A_LIGHT,
        OUT_2A_LIGHT,
        OUT_3A_LIGHT,
        OUT_4A_LIGHT,
        OUT_5A_LIGHT,
        OUT_6A_LIGHT,
        OUT_1B_LIGHT,
        OUT_2B_LIGHT,
        OUT_3B_LIGHT,
        OUT_4B_LIGHT,
        OUT_5B_LIGHT,
        OUT_6B_LIGHT,
        NUM_LIGHTS
    };

    krono::HwEngine eng;

    dsp::SchmittTrigger tapTrig;
    dsp::SchmittTrigger swapCvTrig;
    dsp::SchmittTrigger clockTrig;

    uint32_t lastTapMs = UINT32_MAX;
    std::array<uint32_t, 3> tapIntervalsMs = {};
    uint8_t tapIntervalCount = 0;
    uint32_t lastTapEventMs = 0;

    uint32_t extLastRiseMs = 0;
    uint32_t extLastIsrMs = 0;
    std::array<uint32_t, 3> extIntervalsMs = {};
    uint8_t extIntervalIdx = 0;

    bool external_clock_active = false;
    uint32_t last_valid_external_clock_interval = 0;
    uint32_t last_reported_tap_interval = 0;

    int last_known_main_op_mode = 0;

    OpSmState op_sm = OpSmState::Idle;
    uint32_t tap_press_start_time = 0;
    uint8_t op_mode_clicks_count = 0;
    bool just_exited_op_mode_sm = false;
    uint32_t tap_release_time_for_timeout_logic = 0;
    bool mod_pressed_during_tap_hold_phase = false;
    uint32_t mode_confirm_state_enter_time = 0;
    bool tap_confirm_action_taken_this_press = false;

    uint32_t pa1_mod_change_last_event_time = 0;
    bool pa1_mod_change_last_debounced_state = false;
    bool pa1_mod_change_current_raw_state = false;
    bool pa1_mod_change_last_raw_state = false;

    CalcSwapSmState calc_swap_sm = CalcSwapSmState::Idle;
    uint32_t calc_swap_mode_press_start_time = 0;
    uint32_t last_calc_swap_trigger_time = 0;

    HwStatusLed status_led;
    uint32_t aux_led_until_ms = 0;

    void pulse_aux(uint32_t now) { aux_led_until_ms = now + kAuxPulseMs; }

    void reset_op_mode_sm_vars(uint32_t now) {
        op_sm = OpSmState::Idle;
        tap_press_start_time = 0;
        op_mode_clicks_count = 0;
        status_led.set_override(false, false, now);
        tap_release_time_for_timeout_logic = 0;
        mod_pressed_during_tap_hold_phase = false;
        mode_confirm_state_enter_time = 0;

        pa1_mod_change_last_event_time = 0;
        pa1_mod_change_last_debounced_state = false;
        pa1_mod_change_current_raw_state = false;
        pa1_mod_change_last_raw_state = false;

        just_exited_op_mode_sm = true;
    }

    void reset_calc_swap_sm_vars() {
        calc_swap_sm = CalcSwapSmState::Idle;
        calc_swap_mode_press_start_time = 0;
    }

    void handle_button_calc_mode_swap(uint32_t now, bool mod_is_pressed_raw) {
        switch (calc_swap_sm) {
            case CalcSwapSmState::Idle:
                if (mod_is_pressed_raw) {
                    if (op_sm == OpSmState::Idle) {
                        calc_swap_sm = CalcSwapSmState::ModePressed;
                        calc_swap_mode_press_start_time = now;
                    }
                }
                break;
            case CalcSwapSmState::ModePressed:
                if (!mod_is_pressed_raw) {
                    if (now - calc_swap_mode_press_start_time <= kCalcSwapMaxMs) {
                        if (now - last_calc_swap_trigger_time > kCalcSwapCooldownMs) {
                            eng.toggle_calc_mode();
                            pulse_aux(now);
                            last_calc_swap_trigger_time = now;
                        }
                    }
                    reset_calc_swap_sm_vars();
                } else {
                    if (now - calc_swap_mode_press_start_time > kCalcSwapMaxMs)
                        reset_calc_swap_sm_vars();
                }
                break;
        }
    }

    void handle_op_mode_sm(uint32_t now, bool tap_pressed_now, bool mod_is_pressed_raw) {
        pa1_mod_change_current_raw_state = mod_is_pressed_raw;
        if (pa1_mod_change_current_raw_state != pa1_mod_change_last_raw_state)
            pa1_mod_change_last_event_time = now;
        pa1_mod_change_last_raw_state = pa1_mod_change_current_raw_state;

        const bool old_debounced_mod_state = pa1_mod_change_last_debounced_state;
        if ((now - pa1_mod_change_last_event_time) > kPa1DebounceMs) {
            if (pa1_mod_change_current_raw_state != pa1_mod_change_last_debounced_state)
                pa1_mod_change_last_debounced_state = pa1_mod_change_current_raw_state;
        }
        const bool mod_button_is_debounced_pressed = pa1_mod_change_last_debounced_state;
        const bool mod_button_just_pressed_debounced =
            (mod_button_is_debounced_pressed && !old_debounced_mod_state);
        const bool mod_button_just_released_debounced =
            (!mod_button_is_debounced_pressed && old_debounced_mod_state);

        switch (op_sm) {
            case OpSmState::Idle:
                if (just_exited_op_mode_sm && !tap_pressed_now)
                    just_exited_op_mode_sm = false;
                if (!just_exited_op_mode_sm && tap_pressed_now && calc_swap_sm == CalcSwapSmState::Idle) {
                    op_sm = OpSmState::TapHeldQualifying;
                    tap_press_start_time = now;
                    tapIntervalCount = 0;
                    lastTapMs = UINT32_MAX;
                    pa1_mod_change_last_debounced_state = mod_is_pressed_raw;
                    pa1_mod_change_last_event_time = now;
                }
                break;

            case OpSmState::TapHeldQualifying:
                if (!tap_pressed_now) {
                    reset_op_mode_sm_vars(now);
                } else if (now - tap_press_start_time >= kOpModeTapHoldMs) {
                    mod_pressed_during_tap_hold_phase = false;
                    op_mode_clicks_count = 0;
                    status_led.set_override(true, true, now);
                    pulse_aux(now);
                    op_sm = OpSmState::TapQualifiedWaitingRelease;
                }
                break;

            case OpSmState::TapQualifiedWaitingRelease:
                if (tap_pressed_now) {
                    if (mod_button_is_debounced_pressed)
                        status_led.set_override(true, false, now);
                    else
                        status_led.set_override(true, true, now);
                    if (mod_button_just_released_debounced) {
                        op_mode_clicks_count++;
                        mod_pressed_during_tap_hold_phase = true;
                    }
                } else {
                    status_led.set_override(true, true, now);
                    if (mod_pressed_during_tap_hold_phase) {
                        if (op_mode_clicks_count > 0) {
                            op_sm = OpSmState::AwaitingConfirmTap;
                            mode_confirm_state_enter_time = now;
                            tap_confirm_action_taken_this_press = false;
                        } else {
                            reset_op_mode_sm_vars(now);
                        }
                    } else {
                        tap_release_time_for_timeout_logic = now;
                        op_sm = OpSmState::AwaitingModPressOrTimeout;
                    }
                }
                break;

            case OpSmState::AwaitingModPressOrTimeout:
                if (mod_button_just_pressed_debounced)
                    tap_release_time_for_timeout_logic = 0;
                if (mod_button_is_debounced_pressed)
                    status_led.set_override(true, false, now);
                else
                    status_led.set_override(true, true, now);
                if (mod_button_just_released_debounced) {
                    op_mode_clicks_count = 1;
                    op_sm = OpSmState::AwaitingConfirmTap;
                    mode_confirm_state_enter_time = now;
                    tap_confirm_action_taken_this_press = false;
                    return;
                }
                if (tap_release_time_for_timeout_logic != 0 &&
                    (now - tap_release_time_for_timeout_logic >= kOpModeSaveTimeoutMs)) {
                    pulse_aux(now);
                    reset_op_mode_sm_vars(now);
                }
                break;

            case OpSmState::AwaitingConfirmTap:
                if (mod_button_is_debounced_pressed)
                    status_led.set_override(true, false, now);
                else
                    status_led.set_override(true, true, now);
                if (mod_button_just_released_debounced) {
                    op_mode_clicks_count++;
                    mode_confirm_state_enter_time = now;
                }

                if (tap_pressed_now) {
                    if (!tap_confirm_action_taken_this_press) {
                        if (op_mode_clicks_count > 0) {
                            pulse_aux(now);
                            const uint8_t clicks = op_mode_clicks_count;
                            if (clicks > 0 && clicks <= (uint8_t)kNumOpModes)
                                eng.set_op_mode((krono::OpMode)(clicks - 1));
                        }
                        reset_op_mode_sm_vars(now);
                        tap_confirm_action_taken_this_press = true;
                    }
                } else {
                    tap_confirm_action_taken_this_press = false;
                }

                if (now - mode_confirm_state_enter_time >= kOpModeConfirmTimeoutMs)
                    reset_op_mode_sm_vars(now);
                break;

            default:
                reset_op_mode_sm_vars(now);
                break;
        }
    }

    Krono() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        configButton(TAP_PARAM, "Tap");
        configButton(MOD_PARAM, "MOD");

        configInput(CLOCK_INPUT, "External clock");
        configInput(SWAP_INPUT, "Swap / bank CV");

        const char* labels[NUM_OUTPUTS] = {
            "1A", "2A", "3A", "4A", "5A", "6A",
            "1B", "2B", "3B", "4B", "5B", "6B"};
        for (int i = 0; i < NUM_OUTPUTS; i++)
            configOutput(i, labels[i]);

        configLight(STATUS_LED_LIGHT, "Status");
        configLight(AUX_LED_LIGHT, "Aux");
        const char* ledLabels[NUM_OUTPUTS] = {
            "1A LED", "2A LED", "3A LED", "4A LED", "5A LED", "6A LED",
            "1B LED", "2B LED", "3B LED", "4B LED", "5B LED", "6B LED"};
        for (int i = 0; i < NUM_OUTPUTS; i++)
            configLight(OUT_1A_LIGHT + i, ledLabels[i]);
    }

    void onAdd(const AddEvent& e) override { (void)e; }

    json_t* dataToJson() override {
        json_t* rootJ = json_object();
        json_object_set_new(rootJ, "tempoIntervalMs", json_integer(eng.active_tempo_interval_ms));
        json_object_set_new(rootJ, "opMode", json_integer((int)eng.op_mode));
        json_object_set_new(rootJ, "swingA", json_integer(eng.swing_profile_a));
        json_object_set_new(rootJ, "swingB", json_integer(eng.swing_profile_b));
        json_object_set_new(rootJ, "chaosDivisor", json_integer(eng.chaos_divisor));
        json_object_set_new(rootJ, "binaryBank", json_integer(eng.binary_bank));
        json_t* arr = json_array();
        for (int i = 0; i < 12; i++)
            json_array_append_new(arr, json_integer((int)eng.calc_mode_per_op[i]));
        json_object_set_new(rootJ, "calcPerOp", arr);
        return rootJ;
    }

    void dataFromJson(json_t* rootJ) override {
        json_t* j;
        if ((j = json_object_get(rootJ, "swingA")))
            eng.swing_profile_a = (uint32_t)json_integer_value(j);
        if ((j = json_object_get(rootJ, "swingB")))
            eng.swing_profile_b = (uint32_t)json_integer_value(j);
        if ((j = json_object_get(rootJ, "chaosDivisor")))
            eng.chaos_divisor = (uint32_t)json_integer_value(j);
        if ((j = json_object_get(rootJ, "binaryBank")))
            eng.binary_bank = (uint8_t)json_integer_value(j);
        if ((j = json_object_get(rootJ, "calcPerOp")) && json_is_array(j)) {
            size_t idx;
            json_t* el;
            json_array_foreach(j, idx, el) {
                if (idx < 12)
                    eng.calc_mode_per_op[idx] = json_integer_value(el) ? krono::CalcMode::Swapped : krono::CalcMode::Normal;
            }
        }
        if ((j = json_object_get(rootJ, "opMode"))) {
            int m = (int)json_integer_value(j);
            eng.set_op_mode((krono::OpMode)kClamp(m, 0, kNumOpModes - 1));
        }
        if ((j = json_object_get(rootJ, "tempoIntervalMs")))
            eng.active_tempo_interval_ms = (uint32_t)json_integer_value(j);
        eng.calc_mode = eng.calc_mode_per_op[(int)eng.op_mode];
        last_reported_tap_interval = eng.active_tempo_interval_ms;
        last_known_main_op_mode = (int)eng.op_mode;
    }

    void onReset() override {
        Module::onReset();
        eng.on_reset();
        tapIntervalCount = 0;
        extIntervalIdx = 0;
        lastTapMs = UINT32_MAX;
        extLastRiseMs = extLastIsrMs = 0;
        external_clock_active = false;
        last_valid_external_clock_interval = 0;
        last_reported_tap_interval = kDefaultTempoIv;
        last_known_main_op_mode = 0;
        reset_calc_swap_sm_vars();
        reset_op_mode_sm_vars(0);
        just_exited_op_mode_sm = false;
        aux_led_until_ms = 0;
    }

    void process(const ProcessArgs& args) override {
        const uint32_t nowMs = eng.current_time_ms();

        const bool tap_held = params[TAP_PARAM].getValue() > 0.5f;
        const bool mod_raw = params[MOD_PARAM].getValue() > 0.5f;

        handle_op_mode_sm(nowMs, tap_held, mod_raw);
        last_known_main_op_mode = (int)eng.op_mode;

        if (op_sm != OpSmState::Idle) {
            if (calc_swap_sm != CalcSwapSmState::Idle)
                reset_calc_swap_sm_vars();
        } else {
            if (inputs[CLOCK_INPUT].isConnected()) {
                const float cv = inputs[CLOCK_INPUT].getVoltage();
                if (clockTrig.process(cv)) {
                    if (extLastRiseMs != 0) {
                        uint32_t iv = nowMs - extLastRiseMs;
                        if (iv >= kFwMinInterval && iv <= kFwMaxInterval) {
                            extIntervalsMs[extIntervalIdx++] = iv;
                            if (extIntervalIdx >= 3) {
                                uint64_t sum = 0;
                                uint32_t mn = UINT32_MAX, mx = 0;
                                for (int i = 0; i < 3; i++) {
                                    sum += extIntervalsMs[i];
                                    mn = std::min(mn, extIntervalsMs[i]);
                                    mx = std::max(mx, extIntervalsMs[i]);
                                }
                                if (mx - mn <= kFwMaxDiff) {
                                    uint32_t avg = (uint32_t)(sum / 3);
                                    avg = kClamp(avg, kFwMinInterval, kFwMaxInterval);
                                    if (!external_clock_active || avg != last_valid_external_clock_interval) {
                                        eng.on_external_validated(avg, nowMs);
                                    }
                                    external_clock_active = true;
                                    last_valid_external_clock_interval = avg;
                                    tapIntervalCount = 0;
                                    lastTapMs = UINT32_MAX;
                                }
                                extIntervalIdx = 0;
                            }
                        } else {
                            extIntervalIdx = 0;
                        }
                    }
                    extLastRiseMs = nowMs;
                    extLastIsrMs = nowMs;
                }
                if (extLastIsrMs != 0 && nowMs > extLastIsrMs + kExtTimeoutMs) {
                    if (external_clock_active) {
                        external_clock_active = false;
                        uint32_t new_iv = kDefaultTempoIv;
                        if (last_valid_external_clock_interval > 0 &&
                            last_valid_external_clock_interval >= kFwMinInterval &&
                            last_valid_external_clock_interval <= kFwMaxInterval) {
                            new_iv = last_valid_external_clock_interval;
                        } else if (last_reported_tap_interval > 0 &&
                                   last_reported_tap_interval >= kFwMinInterval &&
                                   last_reported_tap_interval <= kFwMaxInterval) {
                            new_iv = last_reported_tap_interval;
                        }
                        last_reported_tap_interval = new_iv;
                        eng.on_external_timeout();
                        eng.set_tempo_ms(new_iv, false, nowMs);
                        last_valid_external_clock_interval = 0;
                    }
                }
            } else {
                extLastRiseMs = extLastIsrMs = 0;
                extIntervalIdx = 0;
                if (external_clock_active) {
                    external_clock_active = false;
                    eng.on_external_timeout();
                    uint32_t new_iv = kDefaultTempoIv;
                    if (last_valid_external_clock_interval > 0 &&
                        last_valid_external_clock_interval >= kFwMinInterval &&
                        last_valid_external_clock_interval <= kFwMaxInterval) {
                        new_iv = last_valid_external_clock_interval;
                    } else if (last_reported_tap_interval > 0 &&
                               last_reported_tap_interval >= kFwMinInterval &&
                               last_reported_tap_interval <= kFwMaxInterval) {
                        new_iv = last_reported_tap_interval;
                    }
                    last_reported_tap_interval = new_iv;
                    eng.set_tempo_ms(new_iv, false, nowMs);
                    last_valid_external_clock_interval = 0;
                }
            }

            if (!external_clock_active) {
                handle_button_calc_mode_swap(nowMs, mod_raw);

                if (tapTrig.process(params[TAP_PARAM].getValue())) {
                    if (lastTapMs != UINT32_MAX) {
                        uint32_t iv = nowMs - lastTapMs;
                        if (iv >= kFwMinInterval && iv <= kFwMaxInterval) {
                            tapIntervalsMs[tapIntervalCount++] = iv;
                            if (tapIntervalCount >= 3) {
                                uint64_t sum = 0;
                                uint32_t mn = UINT32_MAX, mx = 0;
                                for (int i = 0; i < 3; i++) {
                                    sum += tapIntervalsMs[i];
                                    mn = std::min(mn, tapIntervalsMs[i]);
                                    mx = std::max(mx, tapIntervalsMs[i]);
                                }
                                if (mx - mn <= kFwMaxDiff) {
                                    uint32_t avg = (uint32_t)(sum / 3);
                                    avg = kClamp(avg, kFwMinInterval, kFwMaxInterval);
                                    eng.set_tempo_ms(avg, false, nowMs);
                                    last_reported_tap_interval = avg;
                                }
                                tapIntervalCount = 0;
                            }
                        } else {
                            tapIntervalCount = 0;
                        }
                    }
                    lastTapMs = nowMs;
                    lastTapEventMs = nowMs;
                }

                if (nowMs > lastTapEventMs + kTapTimeoutMs && lastTapEventMs != 0)
                    tapIntervalCount = 0;

                if (inputs[SWAP_INPUT].isConnected() &&
                    swapCvTrig.process(inputs[SWAP_INPUT].getVoltage())) {
                    if (nowMs - last_calc_swap_trigger_time > kCalcSwapCooldownMs) {
                        eng.toggle_calc_mode();
                        pulse_aux(nowMs);
                        last_calc_swap_trigger_time = nowMs;
                    }
                }
            }
        }

        status_led.sync_mode((int)eng.op_mode, nowMs);
        status_led.update(nowMs);
        lights[STATUS_LED_LIGHT].setBrightness(status_led.out_brightness);
        lights[AUX_LED_LIGHT].setBrightness(nowMs < aux_led_until_ms ? 1.f : 0.f);

        eng.process(args.sampleTime);

        for (int i = 0; i < NUM_OUTPUTS; i++) {
            outputs[i].setVoltage(eng.out_v[i]);
            // Hardware-like jack LEDs: ON when the corresponding output is high.
            lights[OUT_1A_LIGHT + i].setBrightnessSmooth(eng.out_v[i] > 1.f ? 1.f : 0.f, args.sampleTime);
        }
    }
};

struct KronoWidget : ModuleWidget {
    KronoWidget(Krono* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/Krono.svg")));
        box.size = Vec(RACK_GRID_WIDTH * 4.f, RACK_GRID_HEIGHT);

        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2.f * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2.f * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        const float xA = 5.1f;
        const float xB = 15.2f;
        const float ledYOffset = 7.0f;

        addParam(createParamCentered<TL1105>(mm2px(Vec(xA, 19.0)), module, Krono::TAP_PARAM));
        addParam(createParamCentered<TL1105>(mm2px(Vec(xB, 19.0)), module, Krono::MOD_PARAM));

        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(xA, 31.0)), module, Krono::CLOCK_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(xB, 31.0)), module, Krono::SWAP_INPUT));

        // Status/Aux LEDs are in the circles under top jacks.
        addChild(createLightCentered<SmallSimpleLight<RedLight>>(mm2px(Vec(xA, 31.0f + ledYOffset)), module, Krono::STATUS_LED_LIGHT));
        addChild(createLightCentered<SmallSimpleLight<RedLight>>(mm2px(Vec(xB, 31.0f + ledYOffset)), module, Krono::AUX_LED_LIGHT));

        for (int i = 0; i < 6; i++) {
            float y = 49.5f + i * 12.0f;
            addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(xA, y)), module, Krono::OUT_1A_OUTPUT + i));
            addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(xB, y)), module, Krono::OUT_1B_OUTPUT + i));
            addChild(createLightCentered<SmallSimpleLight<BlueLight>>(mm2px(Vec(xA, y + ledYOffset)), module, Krono::OUT_1A_LIGHT + i));
            addChild(createLightCentered<SmallSimpleLight<BlueLight>>(mm2px(Vec(xB, y + ledYOffset)), module, Krono::OUT_1B_LIGHT + i));
        }
    }
};

Model* modelKrono = createModel<Krono, KronoWidget>("Krono");
