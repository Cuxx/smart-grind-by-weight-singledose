#pragma once
#include <driver/i2c_master.h>
#include "../config/constants.h"

struct TouchData {
    uint16_t x;
    uint16_t y;
    bool pressed;
    bool just_pressed;
};

class TouchDriver {
private:
    TouchData last_touch{0, 0, false, false};
    bool initialized = false;
    bool disabled = false;
    bool press_event_pending = false;
    uint8_t dropout_count = 0;
    uint16_t consecutive_i2c_errors = 0;
    uint16_t consecutive_ff_reads = 0;
    uint32_t last_recovery_ms = 0;
    uint32_t total_recoveries = 0;
    uint16_t consecutive_failed_recoveries = 0;
    uint32_t last_health_check_ms = 0;
    uint8_t diag_chip_id = 0;
    uint8_t diag_dev_mode = 0;
    uint8_t diag_ctrl = 0;
    uint8_t diag_pmode = 0;
    uint8_t diag_last_buf[5] = {0, 0, 0, 0, 0};
    
    // Touch activity tracking
    uint32_t last_touch_time = 0;

    void hardware_reset();
    bool configure_registers();
    bool init_i2c_bus();
    void recover_i2c_bus(bool force_reinit);
    uint32_t current_recovery_cooldown_ms() const;

public:
    void init();
    void update();
    void disable();
    void enable();
    TouchData get_touch_data() const { return last_touch; }
    bool is_pressed() const { return last_touch.pressed; }
    void consume_press_event();
    
    // Touch activity timing
    uint32_t get_ms_since_last_touch() const;

    // Diagnostics
    bool is_initialized() const { return initialized; }
    bool is_disabled() const { return disabled; }
    uint32_t get_total_recoveries() const { return total_recoveries; }
    uint16_t get_consecutive_errors() const { return consecutive_i2c_errors; }
    uint8_t get_diag_chip_id() const { return diag_chip_id; }
    uint8_t get_diag_dev_mode() const { return diag_dev_mode; }
    uint8_t get_diag_ctrl() const { return diag_ctrl; }
    uint8_t get_diag_pmode() const { return diag_pmode; }
    const uint8_t* get_diag_last_buf() const { return diag_last_buf; }

private:
    i2c_master_bus_handle_t bus_handle = nullptr;
    i2c_master_dev_handle_t device_handle = nullptr;
};
