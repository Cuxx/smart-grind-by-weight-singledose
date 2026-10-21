#pragma once

#include <cstddef>
#include <cstdint>
#include "../config/constants.h"
#include "../config/hardware.h"
#include "../config/user.h"

#if !defined(SMART_GRIND_SIM) && !defined(MOCK_BUILD)
#include <Arduino.h>
#include <driver/rmt_encoder.h>
#include <driver/rmt_tx.h>
#endif

struct RgbColor {
    uint8_t r{0};
    uint8_t g{0};
    uint8_t b{0};

    constexpr RgbColor() : r(0), g(0), b(0) {}
    constexpr RgbColor(uint8_t red, uint8_t green, uint8_t blue) : r(red), g(green), b(blue) {}
    constexpr explicit RgbColor(uint32_t hex)
        : r(static_cast<uint8_t>((hex >> 16) & 0xFF)),
          g(static_cast<uint8_t>((hex >> 8) & 0xFF)),
          b(static_cast<uint8_t>(hex & 0xFF)) {}

    bool operator==(const RgbColor& o) const { return r == o.r && g == o.g && b == o.b; }
    bool operator!=(const RgbColor& o) const { return !(*this == o); }

    static const RgbColor kOff;
    static const RgbColor kRed;
    static const RgbColor kGreen;
    static const RgbColor kBlue;
    static const RgbColor kAmber;
    static const RgbColor kWhite;
};

class RgbLed {
public:
    static constexpr size_t kMaxLeds = 16;

    RgbLed();
    ~RgbLed();

    void init(int pin = HW_RGB_LED_PIN, size_t count = USER_RGB_LED_COUNT);
    void set_all(const RgbColor& color);
    void set_pixel(size_t index, const RgbColor& color);
    void clear();
    void show();

    void set_brightness(uint8_t percent);
    uint8_t get_brightness() const { return brightness_percent_; }

    bool is_initialized() const { return initialized_; }
    size_t get_count() const { return led_count_; }
    RgbColor get_pixel(size_t index) const;

private:
    int pin_;
    size_t led_count_;
    uint8_t brightness_percent_;
    bool initialized_;
    RgbColor colors_[kMaxLeds];
    uint8_t pixel_buffer_[kMaxLeds * 3];

#if !defined(SMART_GRIND_SIM) && !defined(MOCK_BUILD)
    rmt_channel_handle_t rmt_channel_{nullptr};
    rmt_encoder_handle_t bytes_encoder_{nullptr};
    bool rmt_initialized_{false};
#endif

    void update_buffer();
};
