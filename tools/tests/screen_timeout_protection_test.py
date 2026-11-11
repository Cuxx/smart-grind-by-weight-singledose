"""Tests for screen timeout protection and screensaver sequence."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class ScreenTimeoutProtectionTest(unittest.TestCase):
    def test_protected_state_and_idle_reset(self):
        controller_cpp = (ROOT / "src/ui/controllers/screen_timeout_controller.cpp").read_text()
        self.assertIn("state == UIState::GRIND_COMPLETE", controller_cpp)
        self.assertIn("state == UIState::GRIND_TIMEOUT", controller_cpp)
        self.assertIn("state == UIState::MENU", controller_cpp)
        self.assertIn("grinder->is_grinding()", controller_cpp)
        self.assertIn("grind_controller->is_grind_paused()", controller_cpp)
        self.assertIn("idle_reset_at_ms_ = now;", controller_cpp)

        ui_manager_cpp = (ROOT / "src/ui/ui_manager.cpp").read_text()
        self.assertIn("screen_timeout_controller_->reset_idle_timer()", ui_manager_cpp)

        # Compile and verify standalone logic harness
        harness = r'''
#include <cassert>
#include <cstdint>
#include <algorithm>
#include <cmath>

enum class UIState {
    READY,
    GRINDING,
    GRIND_COMPLETE,
    GRIND_TIMEOUT,
    EDIT,
    MENU,
    CALIBRATION,
    CONFIRM,
    PURGE_CONFIRM,
    AUTOTUNING,
    OTA_UPDATE,
    OTA_UPDATE_FAILED
};

enum class DisplayIdleState : uint8_t {
    ACTIVE,
    SCREENSAVER,
    PANEL_OFF,
};

constexpr DisplayIdleState display_idle_state(uint32_t inactive_ms,
                                               uint32_t screensaver_timeout_ms,
                                               bool panel_off_enabled,
                                               uint32_t panel_off_delay_ms) {
    if (inactive_ms < screensaver_timeout_ms) {
        return DisplayIdleState::ACTIVE;
    }
    if (panel_off_enabled &&
        inactive_ms >= screensaver_timeout_ms + panel_off_delay_ms) {
        return DisplayIdleState::PANEL_OFF;
    }
    return DisplayIdleState::SCREENSAVER;
}

struct Grinder {
    bool grinding = false;
    bool is_grinding() const { return grinding; }
};

struct GrindController {
    bool active = false;
    bool paused = false;
    bool is_active() const { return active; }
    bool is_grind_paused() const { return paused; }
};

struct StateMachine {
    UIState state = UIState::READY;
    UIState get_current_state() const { return state; }
};

struct BluetoothManager {
    bool updating = false;
    bool is_updating() const { return updating; }
};

struct GrindingController {
    bool demo = false;
    bool is_demo_active() const { return demo; }
};

struct HardwareManager {
    Grinder grinder;
    Grinder* get_grinder() { return &grinder; }
};

struct UIManager {
    HardwareManager* hardware_manager = nullptr;
    GrindController* grind_controller = nullptr;
    StateMachine* state_machine = nullptr;
    BluetoothManager* bluetooth_manager = nullptr;
    GrindingController* grinding_controller_ = nullptr;
};

class TimeoutChecker {
public:
    UIManager* ui_manager_;
    uint32_t idle_reset_at_ms_ = 0;
    uint32_t last_weight_activity_ms_ = 0;
    uint32_t settings_applied_at_ms_ = 0;

    TimeoutChecker(UIManager* ui) : ui_manager_(ui) {}

    void reset_idle_timer(uint32_t now) {
        idle_reset_at_ms_ = now;
        last_weight_activity_ms_ = now;
    }

    bool is_protected_state() const {
        if (!ui_manager_) return false;
        if (ui_manager_->bluetooth_manager && ui_manager_->bluetooth_manager->is_updating()) return true;
        if (ui_manager_->grinding_controller_ && ui_manager_->grinding_controller_->is_demo_active()) return true;
        if (ui_manager_->hardware_manager) {
            auto* g = ui_manager_->hardware_manager->get_grinder();
            if (g && g->is_grinding()) return true;
        }
        if (ui_manager_->grind_controller) {
            if (ui_manager_->grind_controller->is_active() || ui_manager_->grind_controller->is_grind_paused()) return true;
        }
        if (!ui_manager_->state_machine) return false;
        UIState s = ui_manager_->state_machine->get_current_state();
        return s == UIState::GRINDING ||
               s == UIState::GRIND_COMPLETE ||
               s == UIState::GRIND_TIMEOUT ||
               s == UIState::PURGE_CONFIRM ||
               s == UIState::MENU ||
               s == UIState::CALIBRATION ||
               s == UIState::AUTOTUNING ||
               s == UIState::OTA_UPDATE ||
               s == UIState::OTA_UPDATE_FAILED;
    }

    DisplayIdleState evaluate(uint32_t now, uint32_t ms_since_touch, uint32_t idle_timeout_ms) {
        if (is_protected_state()) {
            idle_reset_at_ms_ = now;
            last_weight_activity_ms_ = now;
            return DisplayIdleState::ACTIVE;
        }
        uint32_t ms_since_weight = now - last_weight_activity_ms_;
        uint32_t ms_since_settings = now - settings_applied_at_ms_;
        uint32_t ms_since_reset = now - idle_reset_at_ms_;
        uint32_t inactive_ms = std::min(std::min(ms_since_touch, ms_since_weight),
                                        std::min(ms_since_settings, ms_since_reset));
        return display_idle_state(inactive_ms, idle_timeout_ms, false, 0);
    }
};

int main() {
    HardwareManager hw;
    GrindController gc;
    StateMachine sm;
    BluetoothManager bm;
    GrindingController gctrl;
    UIManager ui{&hw, &gc, &sm, &bm, &gctrl};
    TimeoutChecker checker(&ui);

    // 1. Ready state after 30s idle -> screensaver triggers
    sm.state = UIState::READY;
    assert(checker.evaluate(30000, 30000, 30000) == DisplayIdleState::SCREENSAVER);

    // 2. Grinder active -> protected even if no touch for 60s
    hw.grinder.grinding = true;
    assert(checker.evaluate(60000, 60000, 30000) == DisplayIdleState::ACTIVE);
    hw.grinder.grinding = false;

    // 3. Grind controller active or paused -> protected
    gc.active = true;
    assert(checker.evaluate(60000, 60000, 30000) == DisplayIdleState::ACTIVE);
    gc.active = false;
    gc.paused = true;
    assert(checker.evaluate(60000, 60000, 30000) == DisplayIdleState::ACTIVE);
    gc.paused = false;

    // 4. UIState in GRIND_COMPLETE or GRIND_TIMEOUT -> protected
    sm.state = UIState::GRIND_COMPLETE;
    assert(checker.evaluate(60000, 60000, 30000) == DisplayIdleState::ACTIVE);
    sm.state = UIState::GRIND_TIMEOUT;
    assert(checker.evaluate(60000, 60000, 30000) == DisplayIdleState::ACTIVE);

    // 5. UIState in MENU -> protected from screensaver
    sm.state = UIState::MENU;
    assert(checker.evaluate(60000, 60000, 30000) == DisplayIdleState::ACTIVE);

    // 6. Transition back to READY -> idle timer must be reset; screensaver must NOT trigger immediately
    sm.state = UIState::READY;
    // Evaluated right when switching to READY (protected state kept idle_reset_at_ms_ updated)
    assert(checker.evaluate(60000, 60000, 30000) == DisplayIdleState::ACTIVE);
    // 10 seconds later on READY -> still active
    assert(checker.evaluate(70000, 70000, 30000) == DisplayIdleState::ACTIVE);
    // 30 seconds after returning to READY -> screensaver triggers
    assert(checker.evaluate(90000, 90000, 30000) == DisplayIdleState::SCREENSAVER);

    return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            cpp, binary = Path(tmp) / "timeout.cpp", Path(tmp) / "timeout"
            cpp.write_text(harness)
            subprocess.run(
                ["g++", "-std=c++17", "-Wall", "-Wextra", "-fsanitize=address,undefined", str(cpp), "-o", str(binary)],
                check=True,
            )
            subprocess.run([str(binary)], check=True, timeout=15)


if __name__ == "__main__":
    unittest.main()
