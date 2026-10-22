#include "grinding_screen_rotor.h"

#include <Arduino.h>
#include <cstdio>
#include <cstring>

namespace {
constexpr uint32_t ROTOR_COLOR = 0x4A4A4A;
constexpr uint32_t ROTOR_BLADE_COLOR = 0x9A9A9A;
constexpr uint32_t ROTOR_ACTIVE_COLOR = 0xD4A017;
constexpr uint32_t ROTOR_COMPLETE_COLOR = 0x00C853;
constexpr uint32_t SCALE_COLOR = 0x3B3B3B;
constexpr uint32_t SCALE_ACCENT_COLOR = 0x6A6A6A;
constexpr uint32_t DOSING_CUP_COLOR = 0x707070;
constexpr uint32_t DOSING_CUP_RIM_COLOR = 0xB8B8B8;
constexpr int ROTOR_CENTER_Y = 170;
}

GrindingScreenRotor::~GrindingScreenRotor() {
    if (motion_timer_) {
        lv_timer_del(motion_timer_);
        motion_timer_ = nullptr;
    }
}

void GrindingScreenRotor::create() {
    screen_ = lv_obj_create(lv_scr_act());
    lv_obj_set_size(screen_, LV_PCT(100), LV_PCT(80));
    lv_obj_align(screen_, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(screen_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(screen_, 0, 0);
    lv_obj_set_style_pad_all(screen_, 0, 0);
    lv_obj_clear_flag(screen_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(screen_, LV_OBJ_FLAG_CLICKABLE);

    profile_label_ = lv_label_create(screen_);
    lv_label_set_text(profile_label_, "DOUBLE");
    lv_obj_set_style_text_font(profile_label_, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(profile_label_, lv_color_hex(THEME_COLOR_SECONDARY), 0);
    lv_obj_align(profile_label_, LV_ALIGN_TOP_MID, 0, 12);

    target_label_ = lv_label_create(screen_);
    lv_label_set_text(target_label_, "Target: 18.0g");
    lv_obj_set_style_text_font(target_label_, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(target_label_, lv_color_hex(THEME_COLOR_TEXT_SECONDARY), 0);
    lv_obj_align(target_label_, LV_ALIGN_TOP_MID, 0, 47);

    lv_obj_t* rotor_ring = lv_obj_create(screen_);
    lv_obj_set_size(rotor_ring, 124, 124);
    lv_obj_align(rotor_ring, LV_ALIGN_TOP_MID, 0, ROTOR_CENTER_Y - 62);
    lv_obj_set_style_radius(rotor_ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(rotor_ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(rotor_ring, 3, 0);
    lv_obj_set_style_border_color(rotor_ring, lv_color_hex(ROTOR_COLOR), 0);
    lv_obj_clear_flag(rotor_ring, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(rotor_ring, LV_OBJ_FLAG_CLICKABLE);

    for (int i = 0; i < 3; ++i) {
        rotor_blades_[i] = lv_obj_create(screen_);
        lv_obj_set_size(rotor_blades_[i], 14, 92);
        lv_obj_align(rotor_blades_[i], LV_ALIGN_TOP_MID, 0, ROTOR_CENTER_Y - 46);
        lv_obj_set_style_radius(rotor_blades_[i], 8, 0);
        lv_obj_set_style_bg_color(rotor_blades_[i], lv_color_hex(ROTOR_BLADE_COLOR), 0);
        lv_obj_set_style_bg_opa(rotor_blades_[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(rotor_blades_[i], 0, 0);
        lv_obj_clear_flag(rotor_blades_[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(rotor_blades_[i], LV_OBJ_FLAG_CLICKABLE);
    }

    lv_obj_t* rotor_hub = lv_obj_create(screen_);
    lv_obj_set_size(rotor_hub, 38, 38);
    lv_obj_align(rotor_hub, LV_ALIGN_TOP_MID, 0, ROTOR_CENTER_Y - 19);
    lv_obj_set_style_radius(rotor_hub, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(rotor_hub, lv_color_hex(0x202020), 0);
    lv_obj_set_style_border_width(rotor_hub, 2, 0);
    lv_obj_set_style_border_color(rotor_hub, lv_color_hex(ROTOR_BLADE_COLOR), 0);
    lv_obj_clear_flag(rotor_hub, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(rotor_hub, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* chute = lv_obj_create(screen_);
    lv_obj_set_size(chute, 18, 28);
    lv_obj_align(chute, LV_ALIGN_TOP_MID, 0, 228);
    lv_obj_set_style_radius(chute, 4, 0);
    lv_obj_set_style_bg_color(chute, lv_color_hex(0x303030), 0);
    lv_obj_set_style_border_width(chute, 0, 0);
    lv_obj_clear_flag(chute, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(chute, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* scale = lv_obj_create(screen_);
    lv_obj_set_size(scale, 172, 24);
    lv_obj_align(scale, LV_ALIGN_TOP_MID, 0, 319);
    lv_obj_set_style_radius(scale, 8, 0);
    lv_obj_set_style_bg_color(scale, lv_color_hex(SCALE_COLOR), 0);
    lv_obj_set_style_border_width(scale, 1, 0);
    lv_obj_set_style_border_color(scale, lv_color_hex(SCALE_ACCENT_COLOR), 0);
    lv_obj_clear_flag(scale, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(scale, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* dosing_cup = lv_obj_create(screen_);
    lv_obj_set_size(dosing_cup, 86, 58);
    lv_obj_align(dosing_cup, LV_ALIGN_TOP_MID, 0, 261);
    lv_obj_set_style_radius(dosing_cup, 10, 0);
    lv_obj_set_style_bg_color(dosing_cup, lv_color_hex(DOSING_CUP_COLOR), 0);
    lv_obj_set_style_border_width(dosing_cup, 2, 0);
    lv_obj_set_style_border_color(dosing_cup, lv_color_hex(DOSING_CUP_RIM_COLOR), 0);
    lv_obj_clear_flag(dosing_cup, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(dosing_cup, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* cup_rim = lv_obj_create(screen_);
    lv_obj_set_size(cup_rim, 104, 14);
    lv_obj_align(cup_rim, LV_ALIGN_TOP_MID, 0, 254);
    lv_obj_set_style_radius(cup_rim, 7, 0);
    lv_obj_set_style_bg_color(cup_rim, lv_color_hex(DOSING_CUP_RIM_COLOR), 0);
    lv_obj_set_style_border_width(cup_rim, 0, 0);
    lv_obj_clear_flag(cup_rim, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(cup_rim, LV_OBJ_FLAG_CLICKABLE);

    weight_label_ = lv_label_create(screen_);
    lv_label_set_text(weight_label_, "0.0g");
    lv_obj_set_style_text_font(weight_label_, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(weight_label_, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);
    lv_obj_align(weight_label_, LV_ALIGN_TOP_MID, 0, 346);

    for (uint32_t i = 0; i < lv_obj_get_child_cnt(screen_); ++i) {
        lv_obj_clear_flag(lv_obj_get_child(screen_, i), LV_OBJ_FLAG_CLICKABLE);
    }

    visible_ = false;
    displayed_progress_ = -1;
    std::snprintf(displayed_weight_text_, sizeof(displayed_weight_text_), "0.0g");
    motion_timer_ = lv_timer_create(motion_timer_cb, TIMER_PERIOD_MS, this);
    lv_timer_pause(motion_timer_);
    lv_obj_add_flag(screen_, LV_OBJ_FLAG_HIDDEN);
}

void GrindingScreenRotor::show() {
    lv_obj_clear_flag(screen_, LV_OBJ_FLAG_HIDDEN);
    visible_ = true;
    resume_motion_if_needed();
}

void GrindingScreenRotor::hide() {
    visible_ = false;
    if (motion_timer_) {
        lv_timer_pause(motion_timer_);
    }
    lv_obj_add_flag(screen_, LV_OBJ_FLAG_HIDDEN);
}

void GrindingScreenRotor::update_profile_name(const char* name) {
    lv_label_set_text(profile_label_, name ? name : "");
}

void GrindingScreenRotor::update_target_weight(float weight) {
    if (time_mode_) return;
    char text[32];
    std::snprintf(text, sizeof(text), "Target: " SYS_WEIGHT_DISPLAY_FORMAT, weight);
    lv_label_set_text(target_label_, text);
}

void GrindingScreenRotor::update_target_weight_text(const char* text) {
    lv_label_set_text(target_label_, text ? text : "");
}

void GrindingScreenRotor::update_target_time(float seconds) {
    target_time_seconds_ = seconds;
    char text[32];
    std::snprintf(text, sizeof(text), "Time: %.1fs", seconds);
    lv_label_set_text(target_label_, text);
}

void GrindingScreenRotor::update_current_weight(float weight) {
    if (time_mode_) return;
    char text[16];
    std::snprintf(text, sizeof(text), SYS_WEIGHT_DISPLAY_FORMAT, weight);
    if (std::strcmp(displayed_weight_text_, text) != 0) {
        std::snprintf(displayed_weight_text_, sizeof(displayed_weight_text_), "%s", text);
        lv_label_set_text(weight_label_, text);
    }
}

void GrindingScreenRotor::update_tare_display() {
    std::snprintf(displayed_weight_text_, sizeof(displayed_weight_text_), "TARE");
    lv_label_set_text(weight_label_, "TARE");
    displayed_progress_ = -1;
}

void GrindingScreenRotor::update_progress(int percent) {
    if (manual_mode_ || !time_mode_ || target_time_seconds_ <= 0.0f) return;
    if (displayed_progress_ == percent) return;
    displayed_progress_ = percent;
    char text[16];
    std::snprintf(text, sizeof(text), "%.1fs", (percent / 100.0f) * target_time_seconds_);
    lv_label_set_text(weight_label_, text);
}

void GrindingScreenRotor::set_completed(bool completed) {
    completed_ = completed;
    lv_obj_set_style_text_color(weight_label_,
                                lv_color_hex(completed ? ROTOR_COMPLETE_COLOR : THEME_COLOR_TEXT_PRIMARY), 0);
    if (completed) {
        set_activity(GrindScreenActivity::STOPPED);
    }
}

void GrindingScreenRotor::set_time_mode(bool enabled) {
    time_mode_ = enabled;
}

void GrindingScreenRotor::set_manual_mode(bool enabled) {
    manual_mode_ = enabled;
    update_target_visibility();
}

void GrindingScreenRotor::set_activity(GrindScreenActivity activity) {
    activity_ = activity;
    if (activity_ == GrindScreenActivity::ACTIVE) {
        rotation_velocity_ = 22.0f;
    } else if (activity_ == GrindScreenActivity::PULSE) {
        rotation_velocity_ = 34.0f;
    } else if (activity_ == GrindScreenActivity::STOPPED) {
        rotation_velocity_ = 0.0f;
    }
    resume_motion_if_needed();
}

void GrindingScreenRotor::motion_timer_cb(lv_timer_t* timer) {
    auto* screen = static_cast<GrindingScreenRotor*>(lv_timer_get_user_data(timer));
    if (screen) screen->update_motion();
}

void GrindingScreenRotor::update_motion() {
    if (!visible_) return;
    if (activity_ == GrindScreenActivity::SETTLING) {
        rotation_velocity_ *= 0.78f;
        if (rotation_velocity_ < 0.4f) {
            rotation_velocity_ = 0.0f;
            if (motion_timer_) lv_timer_pause(motion_timer_);
            return;
        }
    }
    if (rotation_velocity_ <= 0.0f) return;

    rotation_degrees_ += rotation_velocity_;
    if (rotation_degrees_ >= 360.0f) rotation_degrees_ -= 360.0f;
    for (int i = 0; i < 3; ++i) {
        const int32_t angle = static_cast<int32_t>((rotation_degrees_ + i * 120.0f) * 10.0f);
        lv_obj_set_style_transform_rotation(rotor_blades_[i], angle, 0);
        lv_obj_set_style_bg_color(rotor_blades_[i],
                                  lv_color_hex(activity_ == GrindScreenActivity::ACTIVE || activity_ == GrindScreenActivity::PULSE
                                                   ? ROTOR_ACTIVE_COLOR
                                                   : ROTOR_BLADE_COLOR), 0);
    }
}

void GrindingScreenRotor::update_target_visibility() {
    if (!target_label_) return;
    if (manual_mode_) {
        lv_obj_add_flag(target_label_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(target_label_, LV_OBJ_FLAG_HIDDEN);
    }
}

void GrindingScreenRotor::resume_motion_if_needed() {
    if (!visible_ || !motion_timer_) return;
    if (activity_ == GrindScreenActivity::ACTIVE || activity_ == GrindScreenActivity::PULSE ||
        (activity_ == GrindScreenActivity::SETTLING && rotation_velocity_ > 0.0f)) {
        lv_timer_resume(motion_timer_);
    } else {
        lv_timer_pause(motion_timer_);
    }
}
