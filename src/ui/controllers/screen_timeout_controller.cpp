#include "screen_timeout_controller.h"
#include "screensaver_controller.h"

#include <Arduino.h>

#include "../../config/constants.h"
#include "../../config/logging.h"
#include "../../hardware/display_manager.h"
#include "../../hardware/hardware_manager.h"
#include "../../system/display_idle_policy.h"
#include "../ui_manager.h"

ScreenTimeoutController::ScreenTimeoutController(UIManager* manager)
    : ui_manager_(manager)
    , timing_settings_(ScreensaverSettings::load_timing())
    , settings_applied_at_ms_(millis())
    , last_weight_activity_ms_(settings_applied_at_ms_)
    , idle_reset_at_ms_(settings_applied_at_ms_)
    , screen_dimmed_(false)
    , panel_off_(false) {}

void ScreenTimeoutController::register_events() {}

void ScreenTimeoutController::reset_idle_timer() {
    uint32_t now = millis();
    idle_reset_at_ms_ = now;
    last_weight_activity_ms_ = now;
}

void ScreenTimeoutController::update() {
    if (!ui_manager_) {
        return;
    }

    auto* hardware = ui_manager_->hardware_manager;
    if (!hardware) {
        return;
    }

    auto* display = hardware->get_display();
    if (!display) {
        return;
    }

    if (is_protected_state()) {
        restore_normal_display(display);
        uint32_t now = millis();
        idle_reset_at_ms_ = now;
        last_weight_activity_ms_ = now;
        return;
    }

    uint32_t now = millis();
    refresh_settings_if_needed(now);

    auto* touch_driver = display->get_touch_driver();
    if (!touch_driver) {
        return;
    }

    uint32_t ms_since_touch = touch_driver->get_ms_since_last_touch();
    auto* sensor = hardware->get_weight_sensor();
    uint32_t idle_timeout_ms = ScreensaverSettings::idle_timeout_ms(timing_settings_);
    uint32_t display_off_timeout_ms =
        ScreensaverSettings::display_off_timeout_ms(timing_settings_);
    const uint32_t ms_since_settings_applied = now - settings_applied_at_ms_;
    const uint32_t ms_since_idle_reset = now - idle_reset_at_ms_;
    float weight_delta_g = 0.0f;
    if (sensor && sensor->get_weight_delta(1000U, &weight_delta_g) &&
        display_weight_activity_detected(weight_delta_g, timing_settings_.wake_on_weight_threshold_g)) {
        if (screen_dimmed_ || panel_off_) {
            LOG_BLE("[DISPLAY] Weight wake activity: delta=%.2fg (threshold=%.1fg)\n",
                    static_cast<double>(weight_delta_g),
                    static_cast<double>(timing_settings_.wake_on_weight_threshold_g));
        }
        last_weight_activity_ms_ = now;
    }

    // Use the age of the most recent real activity. Comparing the ends of a
    // short weight window rejects isolated scale noise that could otherwise
    // wake the panel repeatedly from a long min/max history.
    const uint32_t ms_since_weight = now - last_weight_activity_ms_;

    if (preview_active_) {
        const bool grace_passed = (now - preview_started_at_ms_ >= 250);
        const bool touch_woke = grace_passed && (ms_since_touch < (now - preview_started_at_ms_));
        const bool weight_woke = grace_passed &&
                                 (timing_settings_.wake_on_weight_threshold_g > 0.0f) &&
                                 (now - last_weight_activity_ms_ < (now - preview_started_at_ms_));
        if (touch_woke || weight_woke) {
            preview_active_ = false;
            LOG_BLE("[DISPLAY] Preview dismissed by %s\n", touch_woke ? "touch" : "weight");
            restore_normal_display(display);
            return;
        }
        return;
    }

    const uint32_t inactive_ms = min(min(ms_since_touch, ms_since_weight),
                                     min(ms_since_settings_applied, ms_since_idle_reset));
    const DisplayIdleState idle_state = display_idle_state(
        inactive_ms,
        idle_timeout_ms,
        timing_settings_.display_off_enabled,
        static_cast<uint32_t>(timing_settings_.display_off_delay_s) * 1000U);
    const bool should_dim = idle_state != DisplayIdleState::ACTIVE;
    const bool should_turn_off = idle_state == DisplayIdleState::PANEL_OFF;

    if (should_turn_off && !panel_off_) {
        if (!screen_dimmed_) {
            float dimmed = USER_SCREEN_BRIGHTNESS_DIMMED;
            if (ui_manager_->menu_controller_) {
                dimmed = ui_manager_->menu_controller_->get_screensaver_brightness();
            }
            display->set_brightness(dimmed);
            screen_dimmed_ = true;
            if (screensaver_controller_ &&
                screensaver_controller_->is_sleep_enabled() &&
                screensaver_controller_->has_image()) {
                screensaver_controller_->show();
            }
        }
        display->set_panel_power(false);
        panel_off_ = true;
        LOG_BLE("[DISPLAY] Panel off after %lus idle (%lus after screensaver)\n",
                static_cast<unsigned long>(display_off_timeout_ms / 1000U),
                static_cast<unsigned long>(timing_settings_.display_off_delay_s));
    } else if (should_dim && !screen_dimmed_) {
        float dimmed = USER_SCREEN_BRIGHTNESS_DIMMED;
        if (ui_manager_->menu_controller_) {
            dimmed = ui_manager_->menu_controller_->get_screensaver_brightness();
        }
        display->set_brightness(dimmed);
        screen_dimmed_ = true;

        // Show screensaver image if enabled
        if (screensaver_controller_ &&
            screensaver_controller_->is_sleep_enabled() &&
            screensaver_controller_->has_image()) {
            screensaver_controller_->show();
        }
        LOG_BLE("[DISPLAY] Idle timeout reached after %lus; dimmed with screensaver=%s\n",
                static_cast<unsigned long>(timing_settings_.idle_timeout_s),
                screensaver_controller_ && screensaver_controller_->is_visible() ? "ON" : "OFF");
    } else if (!should_dim && (screen_dimmed_ || panel_off_)) {
        const bool touch_wake = (ms_since_touch < idle_timeout_ms);
        const bool weight_wake = (ms_since_weight < idle_timeout_ms);
        LOG_BLE("[DISPLAY] Wake from %s (source: %s%s%s, delta=%.2fg, inactive=%lums)\n",
                panel_off_ ? "panel-off" : "screensaver",
                touch_wake ? "touch" : "",
                (touch_wake && weight_wake) ? "+" : "",
                weight_wake ? "weight" : (!touch_wake ? "timer/other" : ""),
                static_cast<double>(weight_delta_g),
                static_cast<unsigned long>(inactive_ms));
        restore_normal_display(display);
    }
}

bool ScreenTimeoutController::apply_runtime_settings(bool verify_storage) {
    ScreensaverTimingSettings loaded;
    if (verify_storage) {
        if (!ScreensaverSettings::load_timing_checked(loaded)) return false;
    } else {
        loaded = ScreensaverSettings::load_timing();
    }
    timing_settings_ = loaded;
    settings_applied_at_ms_ = millis();
    last_weight_activity_ms_ = settings_applied_at_ms_;
    last_settings_refresh_ms_ = settings_applied_at_ms_;
    idle_reset_at_ms_ = settings_applied_at_ms_;
    LOG_BLE("[DISPLAY] Runtime settings applied; screensaver=%lus panel-off=%s delay=%lus wake-weight=%.1fg\n",
            static_cast<unsigned long>(timing_settings_.idle_timeout_s),
            timing_settings_.display_off_enabled ? "ON" : "OFF",
            static_cast<unsigned long>(timing_settings_.display_off_delay_s),
            timing_settings_.wake_on_weight_threshold_g);

    if (!ui_manager_ || !ui_manager_->hardware_manager) return false;
    DisplayManager* display = ui_manager_->hardware_manager->get_display();
    if (!display) return false;
    restore_normal_display(display);
    return true;
}

void ScreenTimeoutController::refresh_settings_if_needed(uint32_t now_ms) {
    constexpr uint32_t kRefreshIntervalMs = 5000;

    if (last_settings_refresh_ms_ != 0 &&
        now_ms - last_settings_refresh_ms_ < kRefreshIntervalMs) {
        return;
    }

    timing_settings_ = ScreensaverSettings::load_timing();
    last_settings_refresh_ms_ = now_ms;
}

void ScreenTimeoutController::restore_normal_display(DisplayManager* display) {
    bool screensaver_visible = screensaver_controller_ && screensaver_controller_->is_visible();
    if (screensaver_visible) {
        screensaver_controller_->hide();
        if (display) {
            display->suppress_wake_touch();
        }
    }

    if (screen_dimmed_ || panel_off_ || screensaver_visible) {
        float normal = USER_SCREEN_BRIGHTNESS_NORMAL;
        if (ui_manager_ && ui_manager_->menu_controller_) {
            normal = ui_manager_->menu_controller_->get_normal_brightness();
        }
        display->set_brightness(normal);
    }

    if (panel_off_ || !display->is_panel_powered_on()) {
        display->set_panel_power(true);
    }

    screen_dimmed_ = false;
    panel_off_ = false;
    preview_active_ = false;
}

void ScreenTimeoutController::trigger_preview() {
    if (!ui_manager_ || !ui_manager_->hardware_manager) return;
    DisplayManager* display = ui_manager_->hardware_manager->get_display();
    if (!display) return;
    if (is_protected_state()) return;

    preview_active_ = true;
    preview_started_at_ms_ = millis();

    float dimmed = USER_SCREEN_BRIGHTNESS_DIMMED;
    if (ui_manager_->menu_controller_) {
        dimmed = ui_manager_->menu_controller_->get_screensaver_brightness();
    }
    display->set_brightness(dimmed);
    screen_dimmed_ = true;

    if (screensaver_controller_ && screensaver_controller_->has_image()) {
        if (screensaver_controller_->is_visible()) {
            screensaver_controller_->hide();
        }
        screensaver_controller_->show();
    }
}

bool ScreenTimeoutController::is_protected_state() const {
    if (!ui_manager_) {
        return false;
    }

    bool ota_active = ui_manager_->bluetooth_manager &&
                      ui_manager_->bluetooth_manager->is_updating();
    if (ota_active) {
        return true;
    }
    bool is_demo = (ui_manager_->grinding_controller_ &&
                    ui_manager_->grinding_controller_->is_demo_active());
    if (is_demo) {
        return true;
    }

    if (ui_manager_->hardware_manager) {
        auto* grinder = ui_manager_->hardware_manager->get_grinder();
        if (grinder && grinder->is_grinding()) {
            return true;
        }
    }

    if (ui_manager_->grind_controller) {
        if (ui_manager_->grind_controller->is_active() ||
            ui_manager_->grind_controller->is_grind_paused()) {
            return true;
        }
    }

    if (!ui_manager_->state_machine) {
        return false;
    }

    UIState state = ui_manager_->state_machine->get_current_state();
    return state == UIState::GRINDING ||
           state == UIState::GRIND_COMPLETE ||
           state == UIState::GRIND_TIMEOUT ||
           state == UIState::PURGE_CONFIRM ||
           state == UIState::MENU ||
           state == UIState::CALIBRATION ||
           state == UIState::AUTOTUNING ||
           state == UIState::OTA_UPDATE ||
           state == UIState::OTA_UPDATE_FAILED;
}
