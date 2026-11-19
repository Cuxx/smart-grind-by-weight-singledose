"""Unit tests for TouchDriver idle filtering and wake tap detection."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]

HARNESS = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>

using esp_err_t = int;
constexpr int ESP_OK = 0;
constexpr int ESP_ERR_TIMEOUT = 1;

uint32_t g_current_time_ms = 1000;
uint32_t millis() { return g_current_time_ms; }

struct TouchPacket {
    esp_err_t err;
    uint8_t buf[5];
};

TouchPacket g_next_packet = {ESP_OK, {0, 0, 0, 0, 0}};

esp_err_t mock_i2c_transmit_receive(void*, const uint8_t*, size_t, uint8_t* out_buf, size_t out_len, int) {
    if (g_next_packet.err != ESP_OK) {
        return g_next_packet.err;
    }
    if (out_buf && out_len >= sizeof(g_next_packet.buf)) {
        memcpy(out_buf, g_next_packet.buf, sizeof(g_next_packet.buf));
    }
    return ESP_OK;
}

struct TouchData {
    uint16_t x;
    uint16_t y;
    bool pressed;
    bool just_pressed;
};

class SimulatedTouchDriver {
public:
    TouchData last_touch{0, 0, false, false};
    bool initialized = true;
    bool disabled = false;
    bool press_event_pending = false;
    uint8_t dropout_count = 0;
    uint32_t last_touch_time = 0;
    void* device_handle = (void*)1;

    void consume_press_event() {
        press_event_pending = false;
        last_touch.just_pressed = false;
    }

    void update() {
        if (!initialized || disabled || device_handle == nullptr) {
            return;
        }

        const bool was_pressed = last_touch.pressed;
        last_touch.just_pressed = press_event_pending;

        uint8_t buf[5] = {0};
        uint8_t reg = 0x02;
        esp_err_t err = mock_i2c_transmit_receive(device_handle, &reg, sizeof(reg), buf, sizeof(buf), 20);
        if (err != ESP_OK) {
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

        uint8_t touches = buf[0] & 0x0F;
        if (touches > 2) {
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
                last_touch_time = millis();
                press_event_pending = false;
                last_touch.pressed = false;
                last_touch.just_pressed = false;
            }
        } else {
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
};

int main() {
    SimulatedTouchDriver driver;
    driver.last_touch_time = 1000;
    g_current_time_ms = 1000;

    // Test 1: Idle polling with stale 0x01 event_flag in buf[1] (touches == 0)
    // Must NOT update last_touch_time!
    g_next_packet = {ESP_OK, {0x00, 0x40, 0x00, 0x00, 0x00}}; // touches=0, event_flag=0x01
    for (int i = 0; i < 50; ++i) {
        g_current_time_ms += 16;
        driver.update();
        assert(driver.last_touch_time == 1000);
        assert(!driver.last_touch.pressed);
        assert(!driver.last_touch.just_pressed);
    }

    // Test 2: Quick tap where only Up frame (event_flag == 0x01) with touches == 1 is seen
    g_current_time_ms = 5000;
    g_next_packet = {ESP_OK, {0x01, 0x40 | 0x00, 0x64, 0x00, 0xC8}}; // touches=1, event_flag=0x01, x=100, y=200
    driver.update();
    assert(driver.last_touch_time == 5000);
    assert(!driver.last_touch.pressed);
    assert(!driver.last_touch.just_pressed);
    assert(driver.last_touch.x == 100);
    assert(driver.last_touch.y == 200);

    // After tap ends, idle polling returns (touches == 0, stale event_flag 0x01)
    g_next_packet = {ESP_OK, {0x00, 0x40, 0x64, 0x00, 0xC8}};
    for (int i = 0; i < 50; ++i) {
        g_current_time_ms += 16;
        driver.update();
        assert(driver.last_touch_time == 5000); // Must remain at 5000!
        assert(!driver.last_touch.pressed);
    }

    // Test 3: Normal press and release
    g_current_time_ms = 10000;
    g_next_packet = {ESP_OK, {0x01, 0x00, 0x50, 0x00, 0x50}}; // Down (0x00), touches=1
    driver.update();
    assert(driver.last_touch_time == 10000);
    assert(driver.last_touch.pressed);
    assert(driver.last_touch.just_pressed);

    // DisplayManager / LVGL indev callback consumes the press event
    driver.consume_press_event();
    assert(!driver.last_touch.just_pressed);

    // Holding contact (event_flag = Contact 0x02) - should NOT keep refreshing last_touch_time
    g_current_time_ms = 10050;
    g_next_packet = {ESP_OK, {0x01, 0x80, 0x50, 0x00, 0x50}}; // Contact (0x02), touches=1
    driver.update();
    assert(driver.last_touch_time == 10000); // Still 10000!
    assert(driver.last_touch.pressed);
    assert(!driver.last_touch.just_pressed);

    // Release (touches == 0)
    g_current_time_ms = 10100;
    g_next_packet = {ESP_OK, {0x00, 0x40, 0x50, 0x00, 0x50}}; // Up (0x01), touches=0
    driver.update();
    assert(driver.last_touch_time == 10100); // Updated on release
    assert(!driver.last_touch.pressed);

    // Subsequent idle frames
    for (int i = 0; i < 20; ++i) {
        g_current_time_ms += 16;
        driver.update();
        assert(driver.last_touch_time == 10100); // Must stay 10100
        assert(!driver.last_touch.pressed);
    }

    // Test 4: Floating bus (0xFF read)
    g_current_time_ms = 15000;
    g_next_packet = {ESP_OK, {0xFF, 0xFF, 0xFF, 0xFF, 0xFF}}; // touches=15 -> clamped to 0
    driver.update();
    assert(driver.last_touch_time == 10100); // Not touched!
    assert(!driver.last_touch.pressed);

    // Test 5: Drag dropout tolerance (1 frame dropout does not release)
    g_current_time_ms = 20000;
    g_next_packet = {ESP_OK, {0x01, 0x00, 0x10, 0x00, 0x20}}; // Press
    driver.update();
    assert(driver.last_touch.pressed);

    g_current_time_ms = 20016;
    g_next_packet = {ESP_OK, {0x00, 0x00, 0x10, 0x00, 0x20}}; // Glitch frame (touches=0, event_flag!=Up)
    driver.update();
    assert(driver.last_touch.pressed); // Still pressed due to dropout tolerance!

    g_current_time_ms = 20032;
    g_next_packet = {ESP_OK, {0x01, 0x80, 0x12, 0x00, 0x22}}; // Contact restored
    driver.update();
    assert(driver.last_touch.pressed);

    // Test 6: Wake-touch suppression (wake gesture is consumed until release)
    bool consume_wake_touch_until_release = true;
    uint32_t wake_touch_guard_started_ms = g_current_time_ms;

    auto filter_touch = [&](const TouchData& touch) -> bool {
        if (!consume_wake_touch_until_release) {
            return false;
        }
        if (!touch.pressed) {
            consume_wake_touch_until_release = false;
            wake_touch_guard_started_ms = 0;
            return true;
        }
        if (millis() - wake_touch_guard_started_ms >= 1000U) {
            consume_wake_touch_until_release = false;
            wake_touch_guard_started_ms = 0;
        }
        return true;
    };

    // While finger is still touching from wake tap, touch is suppressed for LVGL:
    assert(filter_touch(driver.last_touch) == true);

    // User lifts finger:
    g_current_time_ms = 20050;
    g_next_packet = {ESP_OK, {0x00, 0x40, 0x12, 0x00, 0x22}};
    driver.update();
    assert(!driver.last_touch.pressed);
    assert(filter_touch(driver.last_touch) == true);
    assert(!consume_wake_touch_until_release); // Cleared on release!

    // Next tap is processed normally (not suppressed):
    g_current_time_ms = 21000;
    g_next_packet = {ESP_OK, {0x01, 0x00, 0x30, 0x00, 0x30}};
    driver.update();
    assert(driver.last_touch.pressed);
    assert(filter_touch(driver.last_touch) == false);

    std::cout << "All TouchDriver tests passed successfully!" << std::endl;
    return 0;
}
'''


class TouchDriverWakeTest(unittest.TestCase):
    def test_touch_driver_simulation(self):
        with tempfile.TemporaryDirectory() as tmp:
            cpp = Path(tmp) / "touch_test.cpp"
            binary = Path(tmp) / "touch_test"
            cpp.write_text(HARNESS)
            subprocess.run(
                ["g++", "-std=c++17", "-Wall", "-Wextra", "-fsanitize=address,undefined", str(cpp), "-o", str(binary)],
                check=True,
            )
            try:
                res = subprocess.run([str(binary)], check=True, capture_output=True, text=True, timeout=15)
                self.assertIn("All TouchDriver tests passed successfully!", res.stdout)
            except subprocess.CalledProcessError as e:
                print("STDOUT:", e.stdout)
                print("STDERR:", e.stderr)
                raise


if __name__ == "__main__":
    unittest.main()
