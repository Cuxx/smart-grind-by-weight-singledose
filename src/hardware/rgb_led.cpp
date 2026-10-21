#include "rgb_led.h"
#include <algorithm>
#include <cstring>

const RgbColor RgbColor::kOff{0, 0, 0};
const RgbColor RgbColor::kRed{255, 0, 0};
const RgbColor RgbColor::kGreen{0, 255, 0};
const RgbColor RgbColor::kBlue{0, 0, 255};
const RgbColor RgbColor::kAmber{255, 160, 0};
const RgbColor RgbColor::kWhite{255, 255, 255};

RgbLed::RgbLed()
    : pin_(HW_RGB_LED_PIN)
    , led_count_(USER_RGB_LED_COUNT)
    , brightness_percent_(USER_RGB_LED_BRIGHTNESS_PERCENT)
    , initialized_(false) {
    std::fill(std::begin(colors_), std::end(colors_), RgbColor::kOff);
    std::memset(pixel_buffer_, 0, sizeof(pixel_buffer_));
}

RgbLed::~RgbLed() {
#if !defined(SMART_GRIND_SIM) && !defined(MOCK_BUILD)
    if (rmt_initialized_) {
        if (rmt_channel_) {
            rmt_disable(rmt_channel_);
        }
        if (bytes_encoder_) {
            rmt_del_encoder(bytes_encoder_);
            bytes_encoder_ = nullptr;
        }
        if (rmt_channel_) {
            rmt_del_channel(rmt_channel_);
            rmt_channel_ = nullptr;
        }
        rmt_initialized_ = false;
    }
#endif
}

void RgbLed::init(int pin, size_t count) {
    if (initialized_) return;

    pin_ = pin;
    led_count_ = (count == 0) ? 1 : std::min(count, kMaxLeds);
    brightness_percent_ = std::min<uint8_t>(USER_RGB_LED_BRIGHTNESS_PERCENT, 100);

    std::fill(std::begin(colors_), std::end(colors_), RgbColor::kOff);
    std::memset(pixel_buffer_, 0, sizeof(pixel_buffer_));

#if !defined(SMART_GRIND_SIM) && !defined(MOCK_BUILD)
#if USER_RGB_LED_ENABLED
    rmt_tx_channel_config_t tx_chan_config = {
        .gpio_num = static_cast<gpio_num_t>(pin_),
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10000000, // 10MHz = 100ns tick resolution
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
    };

    if (rmt_new_tx_channel(&tx_chan_config, &rmt_channel_) == ESP_OK) {
        rmt_bytes_encoder_config_t encoder_config = {
            .bit0 = {
                .duration0 = 4, // 0.4us HIGH
                .level0 = 1,
                .duration1 = 8, // 0.8us LOW
                .level1 = 0,
            },
            .bit1 = {
                .duration0 = 8, // 0.8us HIGH
                .level0 = 1,
                .duration1 = 4, // 0.4us LOW
                .level1 = 0,
            },
            .flags = {
                .msb_first = 1,
            },
        };

        if (rmt_new_bytes_encoder(&encoder_config, &bytes_encoder_) == ESP_OK) {
            if (rmt_enable(rmt_channel_) == ESP_OK) {
                rmt_initialized_ = true;
            } else {
                rmt_del_encoder(bytes_encoder_);
                bytes_encoder_ = nullptr;
                rmt_del_channel(rmt_channel_);
                rmt_channel_ = nullptr;
            }
        } else {
            rmt_del_channel(rmt_channel_);
            rmt_channel_ = nullptr;
        }
    }
#endif
#endif

    initialized_ = true;
    clear();
    show();
}

void RgbLed::set_all(const RgbColor& color) {
    for (size_t i = 0; i < led_count_; ++i) {
        colors_[i] = color;
    }
}

void RgbLed::set_pixel(size_t index, const RgbColor& color) {
    if (index < led_count_) {
        colors_[index] = color;
    }
}

void RgbLed::clear() {
    set_all(RgbColor::kOff);
}

void RgbLed::set_brightness(uint8_t percent) {
    brightness_percent_ = std::min<uint8_t>(percent, 100);
}

RgbColor RgbLed::get_pixel(size_t index) const {
    if (index < led_count_) {
        return colors_[index];
    }
    return RgbColor::kOff;
}

void RgbLed::update_buffer() {
    for (size_t i = 0; i < led_count_; ++i) {
        const auto& c = colors_[i];
        const uint8_t r = static_cast<uint8_t>((static_cast<uint16_t>(c.r) * brightness_percent_) / 100);
        const uint8_t g = static_cast<uint8_t>((static_cast<uint16_t>(c.g) * brightness_percent_) / 100);
        const uint8_t b = static_cast<uint8_t>((static_cast<uint16_t>(c.b) * brightness_percent_) / 100);

        // WS2812 color sequence is Green, Red, Blue (GRB)
        pixel_buffer_[i * 3 + 0] = g;
        pixel_buffer_[i * 3 + 1] = r;
        pixel_buffer_[i * 3 + 2] = b;
    }
}

void RgbLed::show() {
    if (!initialized_) return;
    update_buffer();

#if !defined(SMART_GRIND_SIM) && !defined(MOCK_BUILD)
    if (!rmt_initialized_ || !rmt_channel_ || !bytes_encoder_) return;

    rmt_transmit_config_t tx_config = {
        .loop_count = 0,
    };

    if (rmt_transmit(rmt_channel_, bytes_encoder_, pixel_buffer_, led_count_ * 3, &tx_config) == ESP_OK) {
        rmt_tx_wait_all_done(rmt_channel_, 10);
    }
#endif
}
