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
    
    // Touch activity tracking
    uint32_t last_touch_time = 0;

    bool init_i2c_bus();
    void recover_i2c_bus(bool force_reinit);

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

private:
    i2c_master_bus_handle_t bus_handle = nullptr;
    i2c_master_dev_handle_t device_handle = nullptr;
};
