#include "screensaver_settings.h"

#include <Preferences.h>
#include <algorithm>

namespace {

constexpr const char* kPrefsNamespace = "screensaver";
constexpr const char* kIdleTimeoutKey = "idle_timeout_s";
constexpr const char* kStartupTimeoutKey = "startup_s";
constexpr const char* kStartupEnabledKey = "startup";
constexpr const char* kSleepEnabledKey = "sleep";
constexpr const char* kDisplayOffEnabledKey = "panel_off";
constexpr const char* kDisplayOffDelayKey = "panel_delay_s";
constexpr const char* kWakeOnWeightKey = "wake_weight_g";

bool get_bool_setting(const char* key, bool default_value) {
    Preferences prefs;
    if (!prefs.begin(kPrefsNamespace, true)) {
        return default_value;
    }
    bool value = prefs.getBool(key, default_value);
    prefs.end();
    return value;
}

}  // namespace

namespace ScreensaverSettings {

bool is_valid_idle_timeout(uint16_t idle_timeout_s) {
    return idle_timeout_s >= kMinIdleTimeoutS && idle_timeout_s <= kMaxIdleTimeoutS;
}

bool is_valid_startup_timeout(uint8_t startup_timeout_s) {
    return startup_timeout_s >= kMinStartupTimeoutS && startup_timeout_s <= kMaxStartupTimeoutS;
}

bool is_valid_display_off_delay(uint16_t display_off_delay_s) {
    return display_off_delay_s >= kMinDisplayOffDelayS &&
           display_off_delay_s <= kMaxDisplayOffDelayS;
}

bool is_valid_wake_on_weight_threshold(float threshold_g) {
    return std::isfinite(threshold_g) &&
           threshold_g >= kMinWakeOnWeightThresholdG &&
           threshold_g <= kMaxWakeOnWeightThresholdG;
}

bool is_startup_enabled() {
    return get_bool_setting(kStartupEnabledKey, false);
}

bool is_sleep_enabled() {
    return get_bool_setting(kSleepEnabledKey, false);
}

ScreensaverTimingSettings load_timing() {
    Preferences prefs;
    ScreensaverTimingSettings settings{
        kDefaultIdleTimeoutS,
        kDefaultStartupTimeoutS,
        false,
        kDefaultDisplayOffDelayS,
        kDefaultWakeOnWeightThresholdG,
    };

    if (!prefs.begin(kPrefsNamespace, true)) {
        return settings;
    }

    settings.idle_timeout_s = prefs.getUShort(kIdleTimeoutKey, kDefaultIdleTimeoutS);
    settings.startup_timeout_s = prefs.getUChar(kStartupTimeoutKey, kDefaultStartupTimeoutS);
    settings.display_off_enabled = prefs.getBool(kDisplayOffEnabledKey, false);
    settings.display_off_delay_s = prefs.getUShort(kDisplayOffDelayKey, kDefaultDisplayOffDelayS);
    settings.wake_on_weight_threshold_g = prefs.getFloat(kWakeOnWeightKey, kDefaultWakeOnWeightThresholdG);
    prefs.end();

    if (!is_valid_idle_timeout(settings.idle_timeout_s)) {
        settings.idle_timeout_s = kDefaultIdleTimeoutS;
    }
    if (!is_valid_startup_timeout(settings.startup_timeout_s)) {
        settings.startup_timeout_s = kDefaultStartupTimeoutS;
    }
    if (!is_valid_display_off_delay(settings.display_off_delay_s)) {
        settings.display_off_delay_s = kDefaultDisplayOffDelayS;
    }
    if (!is_valid_wake_on_weight_threshold(settings.wake_on_weight_threshold_g)) {
        settings.wake_on_weight_threshold_g = kDefaultWakeOnWeightThresholdG;
    }

    return settings;
}

bool load_timing_checked(ScreensaverTimingSettings& settings) {
    Preferences prefs;
    if (!prefs.begin(kPrefsNamespace, true)) return false;
    ScreensaverTimingSettings loaded{};
    loaded.idle_timeout_s = prefs.getUShort(kIdleTimeoutKey, 0);
    loaded.startup_timeout_s = prefs.getUChar(kStartupTimeoutKey, 0);
    const uint8_t enabled = prefs.getUChar(kDisplayOffEnabledKey, 2);
    loaded.display_off_enabled = enabled == 1;
    loaded.display_off_delay_s = prefs.getUShort(kDisplayOffDelayKey, 0);
    loaded.wake_on_weight_threshold_g = prefs.getFloat(kWakeOnWeightKey, kDefaultWakeOnWeightThresholdG);
    prefs.end();
    if (enabled > 1 || !is_valid_idle_timeout(loaded.idle_timeout_s) ||
        !is_valid_startup_timeout(loaded.startup_timeout_s) ||
        !is_valid_display_off_delay(loaded.display_off_delay_s) ||
        !is_valid_wake_on_weight_threshold(loaded.wake_on_weight_threshold_g)) return false;
    settings = loaded;
    return true;
}

bool save_timing(uint16_t idle_timeout_s, uint8_t startup_timeout_s,
                 bool display_off_enabled, uint16_t display_off_delay_s,
                 float wake_on_weight_threshold_g) {
    if (!is_valid_idle_timeout(idle_timeout_s) ||
        !is_valid_startup_timeout(startup_timeout_s) ||
        !is_valid_display_off_delay(display_off_delay_s) ||
        !is_valid_wake_on_weight_threshold(wake_on_weight_threshold_g)) {
        return false;
    }

    Preferences prefs;
    if (!prefs.begin(kPrefsNamespace, false)) {
        return false;
    }

    size_t idle_written = prefs.putUShort(kIdleTimeoutKey, idle_timeout_s);
    size_t startup_written = prefs.putUChar(kStartupTimeoutKey, startup_timeout_s);
    size_t display_off_written = prefs.putBool(kDisplayOffEnabledKey, display_off_enabled);
    size_t display_off_delay_written = prefs.putUShort(kDisplayOffDelayKey, display_off_delay_s);
    size_t wake_written = prefs.putFloat(kWakeOnWeightKey, wake_on_weight_threshold_g);
    prefs.end();

    return idle_written == sizeof(uint16_t) &&
           startup_written == sizeof(uint8_t) &&
           display_off_written == sizeof(bool) &&
           display_off_delay_written == sizeof(uint16_t) &&
           wake_written == sizeof(float);
}

uint32_t idle_timeout_ms(const ScreensaverTimingSettings& settings) {
    return static_cast<uint32_t>(settings.idle_timeout_s) * 1000U;
}

uint32_t display_off_timeout_ms(const ScreensaverTimingSettings& settings) {
    if (!settings.display_off_enabled) {
        return 0;
    }
    return idle_timeout_ms(settings) +
           static_cast<uint32_t>(settings.display_off_delay_s) * 1000U;
}

uint32_t weight_activity_window_ms(const ScreensaverTimingSettings& settings) {
    return std::min(idle_timeout_ms(settings), kWeightActivityWindowCapMs);
}

}  // namespace ScreensaverSettings
