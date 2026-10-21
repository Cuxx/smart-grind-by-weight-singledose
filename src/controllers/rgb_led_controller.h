#pragma once

#include <cstdint>
#include "../config/constants.h"
#include "../config/user.h"
#include "../hardware/rgb_led.h"
#include "grind_events.h"

#if !defined(SMART_GRIND_SIM) && !defined(MOCK_BUILD)
#include <Preferences.h>
#endif

enum class RgbLedState {
    OFF,
    IDLE,
    TARING,
    GRINDING,
    PAUSED,
    COMPLETED,
    TIMEOUT
};

class RgbLedController {
public:
    explicit RgbLedController(RgbLed* led = nullptr);

    void init(RgbLed* led);
    void update(uint32_t now_ms);
    void handle_grind_event(const GrindEventData& event_data, uint32_t now_ms = 0);

    void set_state(RgbLedState state, uint32_t now_ms = 0);
    RgbLedState get_state() const { return current_state_; }

    void set_display_sleep(bool sleeping, uint32_t now_ms = 0);
    bool is_display_sleeping() const { return display_sleeping_; }

    void set_enabled(bool enabled);
    bool is_enabled() const { return enabled_; }

    void set_brightness(uint8_t percent);
    uint8_t get_brightness() const;

    void set_idle_off(bool idle_off) { idle_off_ = idle_off; }
    bool is_idle_off() const { return idle_off_; }

private:
    RgbLed* led_{nullptr};
    RgbLedState current_state_{RgbLedState::IDLE};
    uint32_t state_start_ms_{0};
    uint32_t last_render_ms_{0};

    bool enabled_{USER_RGB_LED_ENABLED};
    bool display_sleeping_{false};
    bool idle_off_{USER_RGB_LED_IDLE_OFF};
    uint32_t complete_hold_ms_{USER_RGB_LED_COMPLETE_HOLD_MS};
    uint32_t error_hold_ms_{USER_RGB_LED_ERROR_HOLD_MS};

    RgbColor last_rendered_color_{RgbColor::kOff};

    void apply_color(const RgbColor& color, bool force = false);
};
