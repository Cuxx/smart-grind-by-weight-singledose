"""Verify touchscreen brightness sliders persist the displayed percentages."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def method(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


class BrightnessSliderPersistenceTest(unittest.TestCase):
    def test_releases_persist_and_press_lost_is_wired(self):
        controller = (ROOT / "src/ui/controllers/menu_controller.cpp").read_text()
        screen = (ROOT / "src/ui/screens/menu_screen.cpp").read_text()
        methods = "\n".join(method(controller, signature) for signature in (
            "void MenuUIController::handle_brightness_normal_slider_released()",
            "void MenuUIController::handle_brightness_screensaver_slider_released()",
        ))

        self.assertEqual(screen.count("LV_EVENT_PRESS_LOST"), 2)
        self.assertEqual(screen.count("ET::BRIGHTNESS_NORMAL_SLIDER_RELEASED"), 2)
        self.assertEqual(screen.count("ET::BRIGHTNESS_SCREENSAVER_SLIDER_RELEASED"), 2)
        self.assertIn("normal_brightness * 100.0f + 0.5f", screen)
        self.assertIn("screensaver_brightness * 100.0f + 0.5f", screen)

        harness = r'''
#include <cassert>
#include <map>
#include <string>
#define LOG_DEBUG_PRINTF(...) ((void)0)
using String = std::string;
constexpr int LV_ANIM_OFF = 0;
constexpr int HW_DISPLAY_MINIMAL_BRIGHTNESS_PERCENT = 15;
struct Slider { int value; };
int lv_slider_get_value(Slider* slider) { return slider->value; }
void lv_slider_set_value(Slider* slider, int value, int) { slider->value = value; }
struct Preferences {
    inline static std::map<String, float> values;
    bool begin(const char*, bool) { return true; }
    size_t putFloat(const char* key, float value) { values[key] = value; return sizeof(float); }
    void end() {}
};
struct MenuScreen {
    Slider normal{72}, screensaver{50};
    Slider* get_brightness_normal_slider() { return &normal; }
    Slider* get_brightness_screensaver_slider() { return &screensaver; }
};
struct Display { void set_brightness(float) {} };
struct HardwareManager {
    Display display;
    Display* get_display() { return &display; }
};
struct UIManager {
    MenuScreen menu_screen;
    HardwareManager hardware;
    HardwareManager* get_hardware_manager() { return &hardware; }
};
struct MenuUIController {
    UIManager* ui_manager_;
    float get_normal_brightness() const;
    void handle_brightness_normal_slider_released();
    void handle_brightness_screensaver_slider_released();
};
''' + methods + r'''
float MenuUIController::get_normal_brightness() const { return 0.72f; }
int main() {
    UIManager ui;
    MenuUIController controller{&ui};
    controller.handle_brightness_normal_slider_released();
    controller.handle_brightness_screensaver_slider_released();
    assert(Preferences::values["normal"] == 0.72f);
    assert(Preferences::values["screensaver"] == 0.50f);

    ui.menu_screen.normal.value = 1;
    controller.handle_brightness_normal_slider_released();
    assert(ui.menu_screen.normal.value == HW_DISPLAY_MINIMAL_BRIGHTNESS_PERCENT);
    assert(Preferences::values["normal"] == 0.15f);
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp) / "brightness.cpp"
            binary = Path(tmp) / "brightness"
            source.write_text(harness)
            subprocess.run(
                ["g++", "-std=c++17", "-Wall", "-Wextra", str(source), "-o", str(binary)],
                check=True,
            )
            subprocess.run([str(binary)], check=True, timeout=10)


if __name__ == "__main__":
    unittest.main()
