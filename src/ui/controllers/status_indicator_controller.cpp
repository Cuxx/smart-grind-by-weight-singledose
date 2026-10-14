#include "status_indicator_controller.h"

#include "../../config/constants.h"
#include "../../controllers/grind_mode.h"
#include "../../network/network_manager.h"
#include "../../network/device_web_server.h"
#include "../../system/diagnostics_controller.h"
#include "../ui_manager.h"

StatusIndicatorController::StatusIndicatorController(UIManager* manager)
    : ui_manager_(manager) {}

void StatusIndicatorController::build() {
    if (!ui_manager_) {
        return;
    }

    if (ble_status_icon_) {
        return;
    }

    // Create BLE status icon (rightmost)
    ble_status_icon_ = lv_label_create(lv_scr_act());
    lv_label_set_text(ble_status_icon_, LV_SYMBOL_BLUETOOTH);
    lv_obj_set_style_text_font(ble_status_icon_, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(ble_status_icon_, lv_color_hex(THEME_COLOR_ACCENT), 0);
    lv_obj_align(ble_status_icon_, LV_ALIGN_TOP_RIGHT, -10, 10);
    lv_obj_add_flag(ble_status_icon_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ble_status_icon_, LV_OBJ_FLAG_CLICKABLE);

    // Wi-Fi container holds the Wi-Fi glyph and 4 signal strength bars
    wifi_container_ = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(wifi_container_);
    lv_obj_set_size(wifi_container_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(wifi_container_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(wifi_container_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(wifi_container_, 3, 0);
    lv_obj_align(wifi_container_, LV_ALIGN_TOP_RIGHT, -36, 10);
    lv_obj_add_flag(wifi_container_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(wifi_container_, LV_OBJ_FLAG_CLICKABLE);

    // Wi-Fi glyph
    wifi_status_icon_ = lv_label_create(wifi_container_);
    lv_label_set_text(wifi_status_icon_, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_font(wifi_status_icon_, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(wifi_status_icon_, lv_color_hex(THEME_COLOR_TEXT_SECONDARY), 0);
    lv_obj_clear_flag(wifi_status_icon_, LV_OBJ_FLAG_CLICKABLE);

    // Bars container with 4 ascending vertical bars
    lv_obj_t* bars_box = lv_obj_create(wifi_container_);
    lv_obj_remove_style_all(bars_box);
    lv_obj_set_size(bars_box, LV_SIZE_CONTENT, 16);
    lv_obj_set_flex_flow(bars_box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bars_box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(bars_box, 2, 0);
    lv_obj_set_style_pad_bottom(bars_box, 3, 0);
    lv_obj_clear_flag(bars_box, LV_OBJ_FLAG_CLICKABLE);

    static const int bar_heights[4] = {4, 7, 10, 13};
    for (int i = 0; i < 4; ++i) {
        wifi_bars_[i] = lv_obj_create(bars_box);
        lv_obj_remove_style_all(wifi_bars_[i]);
        lv_obj_set_size(wifi_bars_[i], 3, bar_heights[i]);
        lv_obj_set_style_radius(wifi_bars_[i], 1, 0);
        lv_obj_set_style_bg_opa(wifi_bars_[i], LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(wifi_bars_[i], lv_color_hex(THEME_COLOR_NEUTRAL), 0);
        lv_obj_clear_flag(wifi_bars_[i], LV_OBJ_FLAG_CLICKABLE);
    }

    // Create warning icon (left of the connection icons)
    warning_icon_ = lv_label_create(lv_scr_act());
    lv_label_set_text(warning_icon_, LV_SYMBOL_WARNING);
    lv_obj_set_style_text_font(warning_icon_, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(warning_icon_, lv_color_hex(THEME_COLOR_WARNING), 0);
    lv_obj_align(warning_icon_, LV_ALIGN_TOP_RIGHT, -84, 10);
    lv_obj_add_flag(warning_icon_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(warning_icon_, LV_OBJ_FLAG_CLICKABLE);

    // A refresh glyph appears only when the background check has found a
    // newer stable release for this exact hardware revision. It is also a
    // generous touch target for starting the update from the grinder.
    firmware_update_icon_ = lv_label_create(lv_scr_act());
    lv_label_set_text(firmware_update_icon_, LV_SYMBOL_REFRESH);
    lv_obj_set_style_text_font(firmware_update_icon_, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(firmware_update_icon_, lv_color_hex(THEME_COLOR_SUCCESS), 0);
    lv_obj_align(firmware_update_icon_, LV_ALIGN_TOP_RIGHT, -118, 10);
    lv_obj_add_flag(firmware_update_icon_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(firmware_update_icon_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(firmware_update_icon_, 12);
    lv_obj_add_event_cb(
        firmware_update_icon_,
        [](lv_event_t* event) {
            if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
            auto* controller = static_cast<StatusIndicatorController*>(lv_event_get_user_data(event));
            if (controller) controller->prompt_firmware_update();
        },
        LV_EVENT_CLICKED, this);

    update_ble_status_icon();
    update_wifi_status_icon();
    update_warning_icon();
    update_firmware_update_icon();
}

void StatusIndicatorController::update() {
    update_ble_status_icon();
    update_wifi_status_icon();
    update_warning_icon();
    update_firmware_update_icon();
}

void StatusIndicatorController::update_firmware_update_icon() {
    if (!ui_manager_ || !firmware_update_icon_) return;
    const bool grinder_idle = !ui_manager_->grind_controller ||
                              !ui_manager_->grind_controller->is_active();
    const int8_t state = device_web_server.firmware_update_available() && grinder_idle ? 1 : 0;
    if (state == last_firmware_update_state_) return;
    last_firmware_update_state_ = state;
    if (state != 0) {
        lv_obj_clear_flag(firmware_update_icon_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(firmware_update_icon_, LV_OBJ_FLAG_HIDDEN);
    }
}

void StatusIndicatorController::prompt_firmware_update() {
    if (!ui_manager_ || !device_web_server.firmware_update_available()) return;
    const String tag = device_web_server.latest_release_tag();
    if (tag.isEmpty()) return;
    const String message = "Install " + tag + " now?\n\nThe grinder will restart.\nDo not remove power.";
    ui_manager_->show_confirmation(
        "UPDATE READY", message.c_str(), "INSTALL", lv_color_hex(THEME_COLOR_SUCCESS),
        [this]() {
            if (!device_web_server.install_available_update()) return;
            ui_manager_->ota_screen.show_ota_mode();
            ui_manager_->ota_screen.update_status("Preparing update...");
            ui_manager_->switch_to_state(UIState::OTA_UPDATE);
        },
        "LATER");
}

void StatusIndicatorController::update_ble_status_icon() {
    if (!ui_manager_ || !ble_status_icon_) {
        return;
    }

    const bool is_in_menu = ui_manager_->state_machine &&
                            ui_manager_->state_machine->is_state(UIState::MENU);

    auto* bluetooth = ui_manager_->bluetooth_manager;
    const int8_t ble_state = !bluetooth || !bluetooth->is_enabled()
                                 ? 0
                                 : bluetooth->is_connected() ? 2 : 1;

    const bool menu_only = bluetooth && bluetooth->is_icon_menu_only();
    const bool is_on_menu_tab = ui_manager_->state_machine &&
                                ui_manager_->state_machine->is_state(UIState::READY) &&
                                ui_manager_->ready_screen.is_menu_tab(ui_manager_->current_tab);

    const int8_t effective_state = is_in_menu
                                       ? 0
                                       : (menu_only && !is_on_menu_tab) ? 0 : ble_state;
    const int current_tab = ui_manager_->current_tab;

    if (effective_state == last_ble_state_ && current_tab == last_ble_visible_tab_) return;
    last_ble_state_ = effective_state;
    last_ble_visible_tab_ = current_tab;

    if (effective_state != 0) {
        lv_obj_clear_flag(ble_status_icon_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_color(ble_status_icon_,
                                    effective_state == 2 ? lv_color_hex(THEME_COLOR_SUCCESS)
                                                         : lv_color_hex(THEME_COLOR_ACCENT),
                                    0);
    } else {
        lv_obj_add_flag(ble_status_icon_, LV_OBJ_FLAG_HIDDEN);
    }
}

void StatusIndicatorController::update_wifi_status_icon() {
    if (!wifi_container_ || !ui_manager_) return;

    // Hard safety interlock: While grinding is active, do NOT poll Wi-Fi RSSI
    // or touch LVGL objects. Full CPU performance remains dedicated to grinding!
    const bool is_grinding = (ui_manager_->grind_controller && ui_manager_->grind_controller->is_active()) ||
                             (ui_manager_->state_machine && ui_manager_->state_machine->is_state(UIState::GRINDING));
    if (is_grinding) {
        return;
    }

    const bool is_in_menu = ui_manager_->state_machine &&
                            ui_manager_->state_machine->is_state(UIState::MENU);

    const int8_t network_state = !network_manager.is_enabled()
                                     ? 0
                                     : network_manager.is_connected() ? 2 : 1;

    const bool menu_only = network_manager.is_icon_menu_only();
    const bool is_on_menu_tab = ui_manager_->state_machine &&
                                ui_manager_->state_machine->is_state(UIState::READY) &&
                                ui_manager_->ready_screen.is_menu_tab(ui_manager_->current_tab);

    const int8_t effective_state = is_in_menu
                                       ? 0
                                       : (menu_only && !is_on_menu_tab) ? 0 : network_state;
    const int current_tab = ui_manager_->current_tab;

    if (effective_state == 0) {
        if (last_wifi_state_ != 0 || current_tab != last_wifi_visible_tab_) {
            last_wifi_state_ = 0;
            last_wifi_visible_tab_ = current_tab;
            lv_obj_add_flag(wifi_container_, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    const uint32_t now = millis();
    const bool state_changed = (effective_state != last_wifi_state_) || (current_tab != last_wifi_visible_tab_);

    // When connected and steady, throttle RSSI polling to every 5 seconds
    if (!state_changed && effective_state == 2 && (now - last_wifi_poll_ms_ < 5000)) {
        return;
    }

    last_wifi_poll_ms_ = now;
    last_wifi_state_ = effective_state;
    last_wifi_visible_tab_ = current_tab;

    lv_obj_clear_flag(wifi_container_, LV_OBJ_FLAG_HIDDEN);

    if (effective_state == 1) {
        // Enabled, but not connected (offline / setup / connecting)
        lv_obj_set_style_text_color(wifi_status_icon_, lv_color_hex(THEME_COLOR_TEXT_SECONDARY), 0);
        for (int i = 0; i < 4; ++i) {
            if (wifi_bars_[i]) {
                lv_obj_set_style_bg_color(wifi_bars_[i], lv_color_hex(THEME_COLOR_NEUTRAL), 0);
            }
        }
        last_wifi_level_ = 0;
        return;
    }

    // Connected: Wi-Fi icon is bright white
    lv_obj_set_style_text_color(wifi_status_icon_, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);

    const int8_t level = network_manager.get_signal_level();
    if (level == last_wifi_level_ && !state_changed) {
        return;
    }
    last_wifi_level_ = level;

    for (int i = 0; i < 4; ++i) {
        if (!wifi_bars_[i]) continue;
        if (i < level) {
            lv_obj_set_style_bg_color(wifi_bars_[i], lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);
        } else {
            lv_obj_set_style_bg_color(wifi_bars_[i], lv_color_hex(THEME_COLOR_NEUTRAL), 0);
        }
    }
}

void StatusIndicatorController::update_warning_icon() {
    if (!ui_manager_ || !warning_icon_) {
        return;
    }

    // Check if there are any diagnostic warnings
    if (ui_manager_->diagnostics_controller_) {
        // All load-cell diagnostics are irrelevant in sensor-free TIME mode,
        // but non-sensor warnings (for example mechanical instability) remain.
        const bool include_load_cell = ui_manager_->current_mode == GrindMode::WEIGHT;
        DiagnosticCode diagnostic =
            ui_manager_->diagnostics_controller_->get_highest_priority_warning(include_load_cell);

        const int8_t state = diagnostic != DiagnosticCode::NONE ? 1 : 0;
        if (state == last_warning_state_) return;
        last_warning_state_ = state;
        if (state != 0) {
            lv_obj_clear_flag(warning_icon_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(warning_icon_, LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        if (last_warning_state_ == 0) return;
        last_warning_state_ = 0;
        lv_obj_add_flag(warning_icon_, LV_OBJ_FLAG_HIDDEN);
    }
}
