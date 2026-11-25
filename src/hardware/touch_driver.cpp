#include "touch_driver.h"
#include "../config/logging.h"
#include <Arduino.h>
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"

namespace {
constexpr uint32_t kTouchI2CFrequencyHz = 400000;
constexpr int kTouchI2CTimeoutMs = 20;
constexpr uint16_t kMaxConsecutiveI2CErrors = 15;
constexpr uint16_t kMaxConsecutiveFFReads = 60;
constexpr uint32_t kRecoveryCooldownMs = 2000;
constexpr char kTag[] = "TouchDriver";

void pulse_i2c_recovery_clocks() {
    const gpio_num_t sda = static_cast<gpio_num_t>(HW_TOUCH_I2C_SDA_PIN);
    const gpio_num_t scl = static_cast<gpio_num_t>(HW_TOUCH_I2C_SCL_PIN);

    gpio_set_direction(sda, GPIO_MODE_INPUT_OUTPUT_OD);
    gpio_set_pull_mode(sda, GPIO_PULLUP_ONLY);
    gpio_set_level(sda, 1);

    gpio_set_direction(scl, GPIO_MODE_INPUT_OUTPUT_OD);
    gpio_set_pull_mode(scl, GPIO_PULLUP_ONLY);
    gpio_set_level(scl, 1);
    delayMicroseconds(5);

    // Send 9 SCL clock pulses to free any slave holding SDA low mid-byte.
    for (int i = 0; i < 9; ++i) {
        gpio_set_level(scl, 0);
        delayMicroseconds(5);
        gpio_set_level(scl, 1);
        delayMicroseconds(5);
    }

    // Generate an explicit STOP condition (SDA low -> SCL high -> SDA high).
    gpio_set_level(sda, 0);
    delayMicroseconds(5);
    gpio_set_level(scl, 1);
    delayMicroseconds(5);
    gpio_set_level(sda, 1);
    delayMicroseconds(5);
}

void suppress_touch_i2c_logs() {
#if DEBUG_SUPPRESS_TOUCH_I2C_ERRORS
    static bool configured = false;
    if (configured) {
        return;
    }
    esp_log_level_set("i2c.master", ESP_LOG_NONE);
    esp_log_level_set("esp32-hal-i2c-ng", ESP_LOG_NONE);
    esp_log_level_set("Wire", ESP_LOG_NONE);
    configured = true;
#endif
}
}

bool TouchDriver::init_i2c_bus() {
    if (bus_handle == nullptr) {
        i2c_master_bus_config_t bus_config = {};
        bus_config.i2c_port = I2C_NUM_0;
        bus_config.sda_io_num = static_cast<gpio_num_t>(HW_TOUCH_I2C_SDA_PIN);
        bus_config.scl_io_num = static_cast<gpio_num_t>(HW_TOUCH_I2C_SCL_PIN);
        bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
        bus_config.glitch_ignore_cnt = 7;
        bus_config.intr_priority = 0;
        bus_config.trans_queue_depth = 0;
        bus_config.flags.enable_internal_pullup = 1;
        bus_config.flags.allow_pd = 0;

        esp_err_t err = i2c_new_master_bus(&bus_config, &bus_handle);
        if (err != ESP_OK) {
            ESP_LOGE(kTag, "Failed to initialize I2C bus: %s", esp_err_to_name(err));
            return false;
        }
    }

    if (device_handle == nullptr) {
        i2c_device_config_t device_config = {};
        device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        device_config.device_address = HW_TOUCH_I2C_ADDRESS;
        device_config.scl_speed_hz = kTouchI2CFrequencyHz;
        device_config.scl_wait_us = 0;
#if DEBUG_SUPPRESS_TOUCH_I2C_ERRORS
        device_config.flags.disable_ack_check = 1;  // Treat idle NACKs as benign when suppression enabled.
#else
        device_config.flags.disable_ack_check = 0;
#endif

        esp_err_t err = i2c_master_bus_add_device(bus_handle, &device_config, &device_handle);
        if (err != ESP_OK) {
            ESP_LOGE(kTag, "Failed to attach touch device: %s", esp_err_to_name(err));
            return false;
        }
    }

    return true;
}

void TouchDriver::recover_i2c_bus(bool force_reinit) {
    last_recovery_ms = millis();
    total_recoveries++;

    esp_err_t reset_err = ESP_FAIL;
    if (!force_reinit && bus_handle != nullptr) {
        reset_err = i2c_master_bus_reset(bus_handle);
    }

    if (force_reinit || reset_err != ESP_OK) {
        if (device_handle != nullptr) {
            i2c_master_bus_rm_device(device_handle);
            device_handle = nullptr;
        }
        if (bus_handle != nullptr) {
            i2c_del_master_bus(bus_handle);
            bus_handle = nullptr;
        }
        pulse_i2c_recovery_clocks();
        const bool reinit_ok = init_i2c_bus();
        LOG_BLE("[TOUCH] I2C full bus re-init #%lu: %s (reset_err=%s)\n",
                static_cast<unsigned long>(total_recoveries),
                reinit_ok ? "OK" : "FAILED",
                esp_err_to_name(reset_err));
    } else {
        LOG_BLE("[TOUCH] I2C bus reset #%lu: OK\n",
                static_cast<unsigned long>(total_recoveries));
    }

    consecutive_i2c_errors = 0;
    consecutive_ff_reads = 0;
    dropout_count = 0;
    press_event_pending = false;
    last_touch.pressed = false;
    last_touch.just_pressed = false;
}

void TouchDriver::init() {
    if (initialized) {
        return;
    }
    suppress_touch_i2c_logs();

    if (!init_i2c_bus()) {
        disabled = true;
        return;
    }
    
    last_touch = {0, 0, false, false};
    press_event_pending = false;
    consecutive_i2c_errors = 0;
    consecutive_ff_reads = 0;
    initialized = true;
    disabled = false;
    
    // Initialize touch tracking
    last_touch_time = millis();
}

void TouchDriver::update() {
    if (!initialized || disabled || device_handle == nullptr) {
        return;
    }
    
    const bool was_pressed = last_touch.pressed;
    last_touch.just_pressed = press_event_pending;

    uint8_t buf[5] = {0};
    uint8_t reg = 0x02; // FT3168_REG_NUM_TOUCHES
    esp_err_t err = i2c_master_transmit_receive(device_handle, &reg, sizeof(reg), buf, sizeof(buf), kTouchI2CTimeoutMs);
    if (err != ESP_OK) {
        consecutive_i2c_errors++;
        if (consecutive_i2c_errors >= kMaxConsecutiveI2CErrors &&
            (millis() - last_recovery_ms >= kRecoveryCooldownMs)) {
            LOG_BLE("[TOUCH] I2C poll error streak=%u (%s); recovering\n",
                    consecutive_i2c_errors, esp_err_to_name(err));
            recover_i2c_bus((total_recoveries % 2) == 1);
            return;
        }
        // Touch controller NACKs when no touch data - treat as no-touch without logging.
        if (err != ESP_ERR_INVALID_STATE && err != ESP_ERR_TIMEOUT) {
            ESP_LOGW(kTag, "Touch poll failed: %s", esp_err_to_name(err));
        }
        if (was_pressed && dropout_count < 1) {
            dropout_count++;
            return;
        }
        dropout_count = 0;
        if (was_pressed) {
            last_touch_time = millis();
        }
        press_event_pending = false;
        last_touch.pressed = false;
        last_touch.just_pressed = false;
        return;
    }

    consecutive_i2c_errors = 0;
    if (buf[0] == 0xFF && buf[1] == 0xFF && buf[2] == 0xFF && buf[3] == 0xFF && buf[4] == 0xFF) {
        consecutive_ff_reads++;
        if (consecutive_ff_reads >= kMaxConsecutiveFFReads &&
            (millis() - last_recovery_ms >= kRecoveryCooldownMs)) {
            LOG_BLE("[TOUCH] Touch controller unresponsive (0xFF streak=%u); recovering\n",
                    consecutive_ff_reads);
            recover_i2c_bus(true);
            return;
        }
    } else {
        consecutive_ff_reads = 0;
    }
    
    uint8_t touches = buf[0] & 0x0F;
    if (touches > 2) {
        // FT3168 supports max 2 touch points. Anything higher indicates bus float/noise.
        touches = 0;
    }
    uint8_t event_flag = (buf[1] >> 6) & 0x03; // 0x00: Down, 0x01: Up, 0x02: Contact, 0x03: No Event

    if (touches > 0) {
        dropout_count = 0;
        last_touch.x = ((buf[1] & 0x0F) << 8) | buf[2];
        last_touch.y = ((buf[3] & 0x0F) << 8) | buf[4];

        if (event_flag != 0x01) {
            last_touch.pressed = true;
            if (!was_pressed) {
                press_event_pending = true;
                last_touch_time = millis();
            }
            last_touch.just_pressed = press_event_pending;
        } else {
            // Explicit lift-up with active touch point reported
            last_touch_time = millis();
            press_event_pending = false;
            last_touch.pressed = false;
            last_touch.just_pressed = false;
        }
    } else {
        // No touch reported without explicit lift-up: tolerate 1-frame glitch during drag
        if (was_pressed && dropout_count < 1 && event_flag != 0x01) {
            dropout_count++;
            return;
        }
        dropout_count = 0;
        if (was_pressed) {
            last_touch_time = millis();
        }
        press_event_pending = false;
        last_touch.pressed = false;
        last_touch.just_pressed = false;
    }
}

void TouchDriver::disable() {
    disabled = true;
    last_touch.pressed = false;  // Clear any active touch state
    last_touch.just_pressed = false;
    press_event_pending = false;
    dropout_count = 0;
}

void TouchDriver::enable() {
    disabled = false;
}

void TouchDriver::consume_press_event() {
    press_event_pending = false;
    last_touch.just_pressed = false;
}

uint32_t TouchDriver::get_ms_since_last_touch() const {
    if (!initialized || disabled) return 0;
    return millis() - last_touch_time;
}
