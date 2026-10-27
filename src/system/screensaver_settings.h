#pragma once

#include <cstdint>
#include "../config/constants.h"

struct ScreensaverTimingSettings {
    uint16_t idle_timeout_s;
    uint8_t startup_timeout_s;
    bool display_off_enabled;
    uint16_t display_off_delay_s;
    float wake_on_weight_threshold_g;
};

namespace ScreensaverSettings {

constexpr uint16_t kDefaultIdleTimeoutS = USER_SCREEN_AUTO_DIM_TIMEOUT_MS / 1000;
constexpr uint8_t kDefaultStartupTimeoutS = 3;
constexpr uint16_t kDefaultDisplayOffDelayS = 3600;
constexpr float kDefaultWakeOnWeightThresholdG = USER_WEIGHT_ACTIVITY_THRESHOLD_G;

constexpr uint16_t kMinIdleTimeoutS = 30;
constexpr uint16_t kMaxIdleTimeoutS = 3600;
constexpr uint8_t kMinStartupTimeoutS = 1;
constexpr uint8_t kMaxStartupTimeoutS = 30;
constexpr uint16_t kMinDisplayOffDelayS = 30;
constexpr uint16_t kMaxDisplayOffDelayS = 43200;
constexpr float kMinWakeOnWeightThresholdG = 0.0f;
constexpr float kMaxWakeOnWeightThresholdG = 50.0f;

constexpr uint32_t kWeightActivityWindowCapMs = 60000;

ScreensaverTimingSettings load_timing();
// Strict post-save read: missing, invalid or unreadable values are failures.
bool load_timing_checked(ScreensaverTimingSettings& settings);
bool save_timing(uint16_t idle_timeout_s, uint8_t startup_timeout_s,
                 bool display_off_enabled, uint16_t display_off_delay_s,
                 float wake_on_weight_threshold_g = kDefaultWakeOnWeightThresholdG);
bool is_valid_idle_timeout(uint16_t idle_timeout_s);
bool is_valid_startup_timeout(uint8_t startup_timeout_s);
bool is_valid_display_off_delay(uint16_t display_off_delay_s);
bool is_valid_wake_on_weight_threshold(float threshold_g);
bool is_startup_enabled();
bool is_sleep_enabled();
uint32_t idle_timeout_ms(const ScreensaverTimingSettings& settings);
uint32_t display_off_timeout_ms(const ScreensaverTimingSettings& settings);
uint32_t weight_activity_window_ms(const ScreensaverTimingSettings& settings);

}  // namespace ScreensaverSettings
