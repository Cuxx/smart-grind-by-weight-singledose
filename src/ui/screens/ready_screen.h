#pragma once
#include <lvgl.h>
#ifdef SMART_GRIND_SIM
#include <string>
using ReadyScreenText = std::string;
#else
#include <Arduino.h>
using ReadyScreenText = String;
#endif
#include "../../config/constants.h"
#include "../../controllers/grind_mode.h"

class ReadyScreen {
private:
    lv_obj_t* screen = nullptr;
    lv_obj_t* tabview = nullptr;
    lv_obj_t* manual_tab = nullptr;
    lv_obj_t* manual_weight_label = nullptr;
    lv_obj_t* manual_scale_status_label = nullptr;
    lv_obj_t* manual_tare_button = nullptr;
    lv_obj_t* manual_safety_label = nullptr;
    lv_obj_t* profile_tabs[3]{};
    lv_obj_t* weight_labels[3]{};
    lv_obj_t* wifi_tab = nullptr;
    lv_obj_t* wifi_status_label = nullptr;
    lv_obj_t* wifi_detail_label = nullptr;
    lv_obj_t* wifi_qr = nullptr;
    lv_obj_t* menu_tab = nullptr;
    ReadyScreenText wifi_status_text;
    ReadyScreenText wifi_detail_text;
    ReadyScreenText wifi_qr_payload;
    bool visible = false;
    bool has_wifi_tab_ = false;

    float cached_profile_values_[3] = {USER_SINGLE_ESPRESSO_WEIGHT_G, USER_DOUBLE_ESPRESSO_WEIGHT_G, USER_CUSTOM_PROFILE_WEIGHT_G};
    GrindMode cached_grind_mode_ = GrindMode::WEIGHT;
    float cached_manual_weight_ = 0.0f;
    bool cached_manual_available_ = true;
    bool cached_manual_taring_ = false;
    uint32_t cached_single_dose_timeout_s_ = 45;
    lv_event_cb_t profile_long_press_handler_ = nullptr;
    lv_event_cb_t manual_tare_handler_ = nullptr;

public:
    static constexpr int MANUAL_TAB_INDEX = 0;
    static constexpr int PROFILE_TAB_START_INDEX = 1;
    static constexpr int PROFILE_TAB_COUNT = 3;

    static bool is_profile_tab(int tab) {
        return tab >= PROFILE_TAB_START_INDEX &&
               tab < PROFILE_TAB_START_INDEX + PROFILE_TAB_COUNT;
    }
    static int profile_index_for_tab(int tab) { return tab - PROFILE_TAB_START_INDEX; }
    static int tab_for_profile_index(int profile) { return profile + PROFILE_TAB_START_INDEX; }

    bool has_wifi_tab() const { return has_wifi_tab_; }
    int wifi_tab_index() const { return has_wifi_tab_ ? 4 : -1; }
    int menu_tab_index() const { return has_wifi_tab_ ? 5 : 4; }
    int tab_count() const { return has_wifi_tab_ ? 6 : 5; }

    bool is_wifi_tab(int tab) const { return has_wifi_tab_ && tab == wifi_tab_index(); }
    bool is_menu_tab(int tab) const { return tab == menu_tab_index(); }

    void create();
    void build_tabs();
    void rebuild_tabs();
    void show();
    void hide();
    void update_profile_values(const float values[3], GrindMode mode);
    void update_manual_scale(float weight, bool available, bool taring);
    void update_single_dose_timeout(uint32_t seconds);
    void update_network_status();
    void set_active_tab(int tab);
    void set_profile_long_press_handler(lv_event_cb_t handler);
    void set_manual_tare_handler(lv_event_cb_t handler);
    
    bool is_visible() const { return visible; }
    lv_obj_t* get_screen() const { return screen; }
    lv_obj_t* get_tabview() const { return tabview; }
    lv_obj_t* get_menu_tab() const { return menu_tab; }
    
private:
    void create_profile_page(lv_obj_t* parent, int profile_index, const char* profile_name, float weight);
    void create_manual_page(lv_obj_t* parent);
    void create_wifi_page(lv_obj_t* parent);
    void create_menu_page(lv_obj_t* parent);
};
