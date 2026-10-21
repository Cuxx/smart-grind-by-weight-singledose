"""Compile and verify RGB LED driver and controller logic."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]

STUBS = r'''
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#define SMART_GRIND_SIM 1
#define HW_RGB_LED_PIN 5
#define USER_RGB_LED_ENABLED true
#define USER_RGB_LED_COUNT 1
#define USER_RGB_LED_BRIGHTNESS_PERCENT 25
#define USER_RGB_LED_IDLE_OFF true
#define USER_RGB_LED_COMPLETE_HOLD_MS 3000
#define USER_RGB_LED_ERROR_HOLD_MS 3000
'''

CASES = r'''
int main() {
    // 1. Test RgbColor constructors and constants
    RgbColor off = RgbColor::kOff;
    assert(off.r == 0 && off.g == 0 && off.b == 0);

    RgbColor red(255, 0, 0);
    assert(red == RgbColor::kRed);
    assert(red != RgbColor::kOff);

    RgbColor hex_red(0xFF0000);
    assert(hex_red == RgbColor::kRed);

    RgbColor hex_green(0x00FF00);
    assert(hex_green == RgbColor::kGreen);

    RgbColor hex_blue(0x0000FF);
    assert(hex_blue == RgbColor::kBlue);

    // 2. Test RgbLed basic operation and multi-LED support
    RgbLed led;
    led.init(5, 3);
    assert(led.is_initialized());
    assert(led.get_count() == 3);
    assert(led.get_brightness() == USER_RGB_LED_BRIGHTNESS_PERCENT);

    // Set all to Green
    led.set_all(RgbColor::kGreen);
    for (size_t i = 0; i < 3; ++i) {
        assert(led.get_pixel(i) == RgbColor::kGreen);
    }

    // Set pixel 1 to Blue
    led.set_pixel(1, RgbColor::kBlue);
    assert(led.get_pixel(0) == RgbColor::kGreen);
    assert(led.get_pixel(1) == RgbColor::kBlue);
    assert(led.get_pixel(2) == RgbColor::kGreen);

    // Test clear
    led.clear();
    for (size_t i = 0; i < 3; ++i) {
        assert(led.get_pixel(i) == RgbColor::kOff);
    }

    // Test brightness scaling
    led.set_brightness(100);
    assert(led.get_brightness() == 100);
    led.set_brightness(50);
    assert(led.get_brightness() == 50);

    // 3. Test RgbLedController state machine
    RgbLed test_led;
    test_led.init(5, 1);
    RgbLedController controller(&test_led);
    controller.init(&test_led);

    assert(controller.get_state() == RgbLedState::IDLE);
    assert(test_led.get_pixel(0) == RgbColor::kOff); // idle_off is true

    // Test Taring state via PHASE_CHANGED
    GrindEventData event{};
    event.event = UIGrindEvent::PHASE_CHANGED;
    event.phase = GrindPhase::TARING;
    event.show_taring_text = true;
    controller.handle_grind_event(event);
    assert(controller.get_state() == RgbLedState::TARING);

    // Test Grinding state via PHASE_CHANGED
    event.phase = GrindPhase::PREDICTIVE;
    event.show_taring_text = false;
    controller.handle_grind_event(event);
    assert(controller.get_state() == RgbLedState::GRINDING);
    assert(test_led.get_pixel(0) == RgbColor::kRed);

    // Test Grinding states for other phases
    for (GrindPhase p : {GrindPhase::TIME_GRINDING, GrindPhase::MANUAL_GRINDING,
                         GrindPhase::TIME_ADDITIONAL_PULSE, GrindPhase::PRIME}) {
        event.phase = p;
        controller.handle_grind_event(event);
        assert(controller.get_state() == RgbLedState::GRINDING);
        assert(test_led.get_pixel(0) == RgbColor::kRed);
    }

    // Test Completion event
    event.event = UIGrindEvent::COMPLETED;
    event.phase = GrindPhase::COMPLETED;
    controller.handle_grind_event(event, 1000);
    assert(controller.get_state() == RgbLedState::COMPLETED);
    assert(test_led.get_pixel(0) == RgbColor::kGreen);

    // Test Completion timer hold (3000ms)
    controller.update(2000); // 1000ms elapsed
    assert(controller.get_state() == RgbLedState::COMPLETED);
    assert(test_led.get_pixel(0) == RgbColor::kGreen);

    controller.update(4000); // 3000ms elapsed -> transitions back to IDLE
    assert(controller.get_state() == RgbLedState::IDLE);
    assert(test_led.get_pixel(0) == RgbColor::kOff);

    // Test Timeout event and hold timer
    event.event = UIGrindEvent::TIMEOUT;
    event.phase = GrindPhase::TIMEOUT;
    controller.handle_grind_event(event, 5000);
    assert(controller.get_state() == RgbLedState::TIMEOUT);

    controller.update(6000); // 1000ms into error -> still TIMEOUT
    assert(controller.get_state() == RgbLedState::TIMEOUT);

    controller.update(8001); // >3000ms into error -> transitions back to IDLE
    assert(controller.get_state() == RgbLedState::IDLE);

    // Test Display Sleep
    controller.set_state(RgbLedState::GRINDING, 7000);
    assert(test_led.get_pixel(0) == RgbColor::kRed);
    controller.set_display_sleep(true, 7100);
    assert(controller.is_display_sleeping());
    assert(test_led.get_pixel(0) == RgbColor::kOff);

    // While sleeping, updates stay off
    controller.update(7200);
    assert(test_led.get_pixel(0) == RgbColor::kOff);

    // Wake display
    controller.set_display_sleep(false, 7300);
    assert(!controller.is_display_sleeping());
    assert(controller.get_state() == RgbLedState::IDLE);

    return 0;
}
'''


def source_without_includes(path):
    return "\n".join(line for line in path.read_text().splitlines()
                     if not line.startswith(("#include", "#pragma once")))


class RgbLedTest(unittest.TestCase):
    def test_driver_and_controller(self):
        code = (STUBS
                + "\n" + source_without_includes(ROOT / "src/controllers/grind_mode.h")
                + "\n" + source_without_includes(ROOT / "src/controllers/grind_events.h")
                + "\n" + source_without_includes(ROOT / "src/hardware/rgb_led.h")
                + "\n" + source_without_includes(ROOT / "src/hardware/rgb_led.cpp")
                + "\n" + source_without_includes(ROOT / "src/controllers/rgb_led_controller.h")
                + "\n" + source_without_includes(ROOT / "src/controllers/rgb_led_controller.cpp")
                + "\n" + CASES)
        with tempfile.TemporaryDirectory(prefix="smart-grind-rgb-test-") as folder:
            source = Path(folder) / "test.cpp"
            binary = Path(folder) / "test"
            source.write_text(code)
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra",
                            "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                            str(source), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=15)


if __name__ == "__main__":
    unittest.main()
