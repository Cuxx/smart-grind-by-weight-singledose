#include "rgb_led_controller.h"
#include <algorithm>
#include <cmath>

RgbLedController::RgbLedController(RgbLed* led)
    : led_(led)
    , current_state_(RgbLedState::IDLE)
    , state_start_ms_(0)
    , last_render_ms_(0)
    , enabled_(USER_RGB_LED_ENABLED)
    , display_sleeping_(false)
    , idle_off_(USER_RGB_LED_IDLE_OFF)
    , complete_hold_ms_(USER_RGB_LED_COMPLETE_HOLD_MS)
    , error_hold_ms_(USER_RGB_LED_ERROR_HOLD_MS)
    , last_rendered_color_(RgbColor::kOff) {}

void RgbLedController::init(RgbLed* led) {
    led_ = led;
    current_state_ = RgbLedState::IDLE;
    state_start_ms_ = 0;
    last_render_ms_ = 0;
    display_sleeping_ = false;
    apply_color(idle_off_ ? RgbColor::kOff : RgbColor{20, 20, 20}, true);
}

void RgbLedController::set_enabled(bool enabled) {
    if (enabled_ == enabled) return;
    enabled_ = enabled;
    if (!enabled_) {
        apply_color(RgbColor::kOff, true);
    } else {
        set_state(current_state_);
    }
}

void RgbLedController::set_brightness(uint8_t percent) {
    if (led_) {
        led_->set_brightness(percent);
        apply_color(last_rendered_color_, true);
    }
}

uint8_t RgbLedController::get_brightness() const {
    return led_ ? led_->get_brightness() : 0;
}

void RgbLedController::set_display_sleep(bool sleeping, uint32_t now_ms) {
    if (display_sleeping_ == sleeping) return;
    display_sleeping_ = sleeping;

    if (display_sleeping_) {
        apply_color(RgbColor::kOff, true);
    } else {
        set_state(RgbLedState::IDLE, now_ms);
    }
}

void RgbLedController::set_state(RgbLedState state, uint32_t now_ms) {
    current_state_ = state;
#if !defined(SMART_GRIND_SIM) && !defined(MOCK_BUILD)
    if (now_ms == 0) now_ms = millis();
#endif
    state_start_ms_ = now_ms;

    if (!enabled_ || display_sleeping_) {
        apply_color(RgbColor::kOff, true);
        return;
    }

    switch (current_state_) {
        case RgbLedState::OFF:
            apply_color(RgbColor::kOff, true);
            break;
        case RgbLedState::IDLE:
            apply_color(idle_off_ ? RgbColor::kOff : RgbColor{20, 20, 20}, true);
            break;
        case RgbLedState::TARING:
            apply_color(RgbColor::kBlue, true);
            break;
        case RgbLedState::GRINDING:
            apply_color(RgbColor::kRed, true);
            break;
        case RgbLedState::PAUSED:
            apply_color(RgbColor::kAmber, true);
            break;
        case RgbLedState::COMPLETED:
            apply_color(RgbColor::kGreen, true);
            break;
        case RgbLedState::TIMEOUT:
            apply_color(RgbColor::kRed, true);
            break;
    }
}

void RgbLedController::handle_grind_event(const GrindEventData& event_data, uint32_t now_ms) {
#if !defined(SMART_GRIND_SIM) && !defined(MOCK_BUILD)
    if (now_ms == 0) now_ms = millis();
#endif
    switch (event_data.event) {
        case UIGrindEvent::PHASE_CHANGED: {
            if (event_data.show_taring_text ||
                event_data.phase == GrindPhase::TARING ||
                event_data.phase == GrindPhase::TARE_CONFIRM) {
                set_state(RgbLedState::TARING, now_ms);
            } else if (event_data.phase == GrindPhase::PREDICTIVE ||
                       event_data.phase == GrindPhase::PULSE_DECISION ||
                       event_data.phase == GrindPhase::PULSE_EXECUTE ||
                       event_data.phase == GrindPhase::PULSE_SETTLING ||
                       event_data.phase == GrindPhase::TIME_GRINDING ||
                       event_data.phase == GrindPhase::MANUAL_GRINDING ||
                       event_data.phase == GrindPhase::TIME_ADDITIONAL_PULSE ||
                       event_data.phase == GrindPhase::PRIME) {
                set_state(RgbLedState::GRINDING, now_ms);
            } else if (event_data.phase == GrindPhase::COMPLETED) {
                set_state(RgbLedState::COMPLETED, now_ms);
            } else if (event_data.phase == GrindPhase::TIMEOUT) {
                set_state(RgbLedState::TIMEOUT, now_ms);
            } else if (event_data.phase == GrindPhase::IDLE) {
                set_state(RgbLedState::IDLE, now_ms);
            }
            break;
        }
        case UIGrindEvent::COMPLETED:
            set_state(RgbLedState::COMPLETED, now_ms);
            break;
        case UIGrindEvent::TIMEOUT:
            set_state(RgbLedState::TIMEOUT, now_ms);
            break;
        case UIGrindEvent::STOPPED:
            set_state(RgbLedState::IDLE, now_ms);
            break;
        case UIGrindEvent::PULSE_STARTED:
            set_state(RgbLedState::GRINDING, now_ms);
            break;
        case UIGrindEvent::PULSE_COMPLETED:
            set_state(RgbLedState::COMPLETED, now_ms);
            break;
        default:
            break;
    }
}

void RgbLedController::update(uint32_t now_ms) {
    if (!enabled_ || display_sleeping_ || !led_ || !led_->is_initialized()) {
        return;
    }

    const uint32_t elapsed = (now_ms >= state_start_ms_) ? (now_ms - state_start_ms_) : 0;

    switch (current_state_) {
        case RgbLedState::COMPLETED: {
            if (elapsed >= complete_hold_ms_) {
                set_state(RgbLedState::IDLE, now_ms);
            } else {
                apply_color(RgbColor::kGreen);
            }
            break;
        }
        case RgbLedState::TIMEOUT: {
            if (elapsed >= error_hold_ms_) {
                set_state(RgbLedState::IDLE, now_ms);
            } else {
                // Blink red at ~2.5 Hz (200ms ON, 200ms OFF)
                const bool blink_on = ((elapsed / 200) % 2) == 0;
                apply_color(blink_on ? RgbColor::kRed : RgbColor::kOff);
            }
            break;
        }
        case RgbLedState::TARING: {
            // Smooth breathing pulse between 20% and 100% blue intensity (1000ms period)
            const uint32_t cycle_ms = elapsed % 1000;
            const float t = (cycle_ms < 500)
                ? (static_cast<float>(cycle_ms) / 500.0f)
                : (static_cast<float>(1000 - cycle_ms) / 500.0f);
            const uint8_t b = static_cast<uint8_t>(50.0f + 205.0f * t);
            apply_color(RgbColor{0, 0, b});
            break;
        }
        case RgbLedState::GRINDING:
            apply_color(RgbColor::kRed);
            break;
        case RgbLedState::PAUSED:
            apply_color(RgbColor::kAmber);
            break;
        case RgbLedState::IDLE:
            apply_color(idle_off_ ? RgbColor::kOff : RgbColor{20, 20, 20});
            break;
        case RgbLedState::OFF:
            apply_color(RgbColor::kOff);
            break;
    }

    last_render_ms_ = now_ms;
}

void RgbLedController::apply_color(const RgbColor& color, bool force) {
    if (!led_) return;
    if (force || color != last_rendered_color_) {
        led_->set_all(color);
        led_->show();
        last_rendered_color_ = color;
    }
}
