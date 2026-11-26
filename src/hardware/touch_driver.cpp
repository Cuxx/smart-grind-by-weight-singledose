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
constexpr uint32_t kHealthCheckIntervalMs = 10000;
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

void TouchDriver::hardware_reset() {
#if defined(HW_TOUCH_RESET_PIN)
    const gpio_num_t rst_pin = static_cast<gpio_num_t>(HW_TOUCH_RESET_PIN);
    gpio_set_direction(rst_pin, GPIO_MODE_OUTPUT);
    gpio_set_level(rst_pin, 1);
    delay(5);
    gpio_set_level(rst_pin, 0);
    delay(20);
    gpio_set_level(rst_pin, 1);
    delay(100);
#endif
}

bool TouchDriver::configure_registers() {
    if (device_handle == nullptr) {
        return false;
    }

    // Register 0x00: DEV_MODE -> 0x00 (Normal Operating Mode)
    uint8_t mode_cmd[2] = {0x00, 0x00};
    esp_err_t err = i2c_master_transmit(device_handle, mode_cmd, sizeof(mode_cmd), kTouchI2CTimeoutMs);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "Failed to set DEV_MODE: %s", esp_err_to_name(err));
    }

    // Register 0x86: ID_G_CTRL -> 0x00 (Active Mode, disable auto-switch to Monitor Mode)
    uint8_t ctrl_cmd[2] = {0x86, 0x00};
    err = i2c_master_transmit(device_handle, ctrl_cmd, sizeof(ctrl_cmd), kTouchI2CTimeoutMs);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "Failed to set ID_G_CTRL: %s", esp_err_to_name(err));
    }

    // Register 0xA5: PMODE -> 0x00 (Active power mode)
    uint8_t pmode_cmd[2] = {0xA5, 0x00};
    err = i2c_master_transmit(device_handle, pmode_cmd, sizeof(pmode_cmd), kTouchI2CTimeoutMs);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "Failed to set PMODE: %s", esp_err_to_name(err));
    }

    // Read chip ID (register 0xA8) to verify responsiveness
    uint8_t id_reg = 0xA8;
    uint8_t chip_id = 0;
    err = i2c_master_transmit_receive(device_handle, &id_reg, sizeof(id_reg), &chip_id, sizeof(chip_id), kTouchI2CTimeoutMs);
    if (err == ESP_OK) {
        ESP_LOGI(kTag, "FT3168 configured: Chip ID 0x%02X", chip_id);
    } else {
        ESP_LOGW(kTag, "FT3168 Chip ID read failed: %s", esp_err_to_name(err));
    }

    return (err == ESP_OK);
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
        device_config.flags.disable_ack_check = 0;

        esp_err_t err = i2c_master_bus_add_device(bus_handle, &device_config, &device_handle);
        if (err != ESP_OK) {
            ESP_LOGE(kTag, "Failed to attach touch device: %s", esp_err_to_name(err));
            return false;
        }
    }

    return true;
}

void TouchDriver::recover_i2c_bus(bool force_reinit) {
    (void)force_reinit;
    last_recovery_ms = millis();
    last_health_check_ms = last_recovery_ms;
    total_recoveries++;

    hardware_reset();

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
    if (reinit_ok) {
        configure_registers();
    }
    LOG_BLE("[TOUCH] I2C recovery #%lu: %s (HW reset + reinit)\n",
            static_cast<unsigned long>(total_recoveries),
            reinit_ok ? "OK" : "FAILED");

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

    hardware_reset();

    if (!init_i2c_bus()) {
        disabled = true;
        return;
    }
    configure_registers();
    
    last_touch = {0, 0, false, false};
    press_event_pending = false;
    consecutive_i2c_errors = 0;
    consecutive_ff_reads = 0;
    initialized = true;
    disabled = false;
    
    // Initialize touch tracking
    last_touch_time = millis();
    last_health_check_ms = last_touch_time;
}

void TouchDriver::update() {
    if (!initialized || disabled || device_handle == nullptr) {
        return;
    }
    
    const bool was_pressed = last_touch.pressed;
    last_touch.just_pressed = press_event_pending;

    // Periodic health check when idle
    const uint32_t now_ms = millis();
    if (!was_pressed && now_ms - last_health_check_ms >= kHealthCheckIntervalMs) {
        last_health_check_ms = now_ms;
        uint8_t id_reg = 0xA8;
        uint8_t chip_id = 0;
        esp_err_t check_err = i2c_master_transmit_receive(device_handle, &id_reg, sizeof(id_reg), &chip_id, sizeof(chip_id), kTouchI2CTimeoutMs);
        if (check_err != ESP_OK || chip_id == 0xFF) {
            LOG_BLE("[TOUCH] Health check failed (%s, id=0x%02X); recovering\n",
                    esp_err_to_name(check_err), chip_id);
            recover_i2c_bus(true);
            return;
        }
        // Re-assert active mode so FT3168 never enters monitor/sleep mode
        uint8_t ctrl_cmd[2] = {0x86, 0x00};
        i2c_master_transmit(device_handle, ctrl_cmd, sizeof(ctrl_cmd), kTouchI2CTimeoutMs);
    }

    uint8_t buf[5] = {0};
    uint8_t reg = 0x02; // FT3168_REG_NUM_TOUCHES
    esp_err_t err = i2c_master_transmit_receive(device_handle, &reg, sizeof(reg), buf, sizeof(buf), kTouchI2CTimeoutMs);
    if (err != ESP_OK) {
        consecutive_i2c_errors++;
        if (consecutive_i2c_errors >= kMaxConsecutiveI2CErrors &&
            (now_ms - last_recovery_ms >= kRecoveryCooldownMs)) {
            LOG_BLE("[TOUCH] I2C poll error streak=%u (%s); recovering\n",
                    consecutive_i2c_errors, esp_err_to_name(err));
            recover_i2c_bus(true);
            return;
        }
        if (err != ESP_ERR_INVALID_STATE && err != ESP_ERR_TIMEOUT) {
            ESP_LOGW(kTag, "Touch poll failed: %s", esp_err_to_name(err));
        }
        if (was_pressed && dropout_count < 1) {
            dropout_count++;
            return;
        }
        dropout_count = 0;
        if (was_pressed) {
            last_touch_time = now_ms;
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
            (now_ms - last_recovery_ms >= kRecoveryCooldownMs)) {
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
                last_touch_time = now_ms;
            }
            last_touch.just_pressed = press_event_pending;
        } else {
            // Explicit lift-up with active touch point reported
            last_touch_time = now_ms;
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
            last_touch_time = now_ms;
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
    last_touch.pressed = false;  // Ensure clean state upon re-enabling
    last_touch.just_pressed = false;
    press_event_pending = false;
    dropout_count = 0;
    last_touch_time = millis();
}

void TouchDriver::consume_press_event() {
    press_event_pending = false;
    last_touch.just_pressed = false;
}

uint32_t TouchDriver::get_ms_since_last_touch() const {
    if (last_touch.pressed) {
        return 0;
    }
    return millis() - last_touch_time;
}
