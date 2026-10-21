#include "grinding_screen_bean_flow.h"

#include <Arduino.h>
#include <cstdio>
#include <cstring>

namespace {
constexpr uint32_t GRINDER_BODY_COLOR = 0x444444;
constexpr uint32_t PORTAFILTER_COLOR = 0xA0A0A0;
constexpr uint32_t BEAN_COLOR = 0x8B5A2B;
constexpr uint32_t FLOW_COMPLETE_COLOR = 0x00C853;
constexpr int PARTICLE_START_Y = 170;
constexpr int PARTICLE_TRAVEL_PX = 82;
}

GrindingScreenBeanFlow::~GrindingScreenBeanFlow() {
    if (motion_timer_) {
        lv_timer_del(motion_timer_);
        motion_timer_ = nullptr;
    }
}

void GrindingScreenBeanFlow::create() {
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
    lv_obj_align(profile_label_, LV_ALIGN_TOP_MID, 0, 10);

    target_label_ = lv_label_create(screen_);
    lv_label_set_text(target_label_, "Target: 18.0g");
    lv_obj_set_style_text_font(target_label_, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(target_label_, lv_color_hex(THEME_COLOR_TEXT_SECONDARY), 0);
    lv_obj_align(target_label_, LV_ALIGN_TOP_MID, 0, 44);

    lv_obj_t* grinder_body = lv_obj_create(screen_);
    lv_obj_set_size(grinder_body, 104, 34);
    lv_obj_align(grinder_body, LV_ALIGN_TOP_MID, 0, 112);
    lv_obj_set_style_radius(grinder_body, 8, 0);
    lv_obj_set_style_bg_color(grinder_body, lv_color_hex(GRINDER_BODY_COLOR), 0);
    lv_obj_set_style_border_width(grinder_body, 1, 0);
    lv_obj_set_style_border_color(grinder_body, lv_color_hex(0x707070), 0);
    lv_obj_clear_flag(grinder_body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(grinder_body, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* chute = lv_obj_create(screen_);
    lv_obj_set_size(chute, 20, 48);
    lv_obj_align(chute, LV_ALIGN_TOP_MID, 0, 140);
    lv_obj_set_style_radius(chute, 4, 0);
    lv_obj_set_style_bg_color(chute, lv_color_hex(0x303030), 0);
    lv_obj_set_style_border_width(chute, 0, 0);
    lv_obj_clear_flag(chute, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(chute, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* portafilter = lv_obj_create(screen_);
    lv_obj_set_size(portafilter, 158, 42);
    lv_obj_align(portafilter, LV_ALIGN_TOP_MID, 0, 244);
    lv_obj_set_style_radius(portafilter, 0, 0);
    lv_obj_set_style_bg_opa(portafilter, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(portafilter, 3, 0);
    lv_obj_set_style_border_color(portafilter, lv_color_hex(PORTAFILTER_COLOR), 0);
    lv_obj_clear_flag(portafilter, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(portafilter, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* handle = lv_obj_create(screen_);
    lv_obj_set_size(handle, 48, 8);
    lv_obj_align(handle, LV_ALIGN_TOP_MID, 0, 285);
    lv_obj_set_style_radius(handle, 4, 0);
    lv_obj_set_style_bg_color(handle, lv_color_hex(PORTAFILTER_COLOR), 0);
    lv_obj_set_style_border_width(handle, 0, 0);
    lv_obj_clear_flag(handle, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(handle, LV_OBJ_FLAG_CLICKABLE);

    for (int i = 0; i < PARTICLE_COUNT; ++i) {
        particles_[i] = lv_obj_create(screen_);
        lv_obj_set_size(particles_[i], 6, 6);
        lv_obj_set_style_radius(particles_[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(particles_[i], lv_color_hex(BEAN_COLOR), 0);
        lv_obj_set_style_border_width(particles_[i], 0, 0);
        lv_obj_clear_flag(particles_[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(particles_[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(particles_[i], LV_OBJ_FLAG_HIDDEN);
    }

    weight_label_ = lv_label_create(screen_);
    lv_label_set_text(weight_label_, "0.0g");
    lv_obj_set_style_text_font(weight_label_, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(weight_label_, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);
    lv_obj_align(weight_label_, LV_ALIGN_TOP_MID, 0, 314);

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

void GrindingScreenBeanFlow::show() {
    lv_obj_clear_flag(screen_, LV_OBJ_FLAG_HIDDEN);
    visible_ = true;
    resume_motion_if_needed();
}

void GrindingScreenBeanFlow::hide() {
    visible_ = false;
    if (motion_timer_) lv_timer_pause(motion_timer_);
    lv_obj_add_flag(screen_, LV_OBJ_FLAG_HIDDEN);
}

void GrindingScreenBeanFlow::update_profile_name(const char* name) {
    lv_label_set_text(profile_label_, name ? name : "");
}

void GrindingScreenBeanFlow::update_target_weight(float weight) {
    if (time_mode_) return;
    char text[32];
    std::snprintf(text, sizeof(text), "Target: " SYS_WEIGHT_DISPLAY_FORMAT, weight);
    lv_label_set_text(target_label_, text);
}

void GrindingScreenBeanFlow::update_target_weight_text(const char* text) {
    lv_label_set_text(target_label_, text ? text : "");
}

void GrindingScreenBeanFlow::update_target_time(float seconds) {
    target_time_seconds_ = seconds;
    char text[32];
    std::snprintf(text, sizeof(text), "Time: %.1fs", seconds);
    lv_label_set_text(target_label_, text);
}

void GrindingScreenBeanFlow::update_current_weight(float weight) {
    if (time_mode_) return;
    char text[16];
    std::snprintf(text, sizeof(text), SYS_WEIGHT_DISPLAY_FORMAT, weight);
    if (std::strcmp(displayed_weight_text_, text) != 0) {
        std::snprintf(displayed_weight_text_, sizeof(displayed_weight_text_), "%s", text);
        lv_label_set_text(weight_label_, text);
    }
}

void GrindingScreenBeanFlow::update_tare_display() {
    std::snprintf(displayed_weight_text_, sizeof(displayed_weight_text_), "TARE");
    lv_label_set_text(weight_label_, "TARE");
    displayed_progress_ = -1;
}

void GrindingScreenBeanFlow::update_progress(int percent) {
    if (manual_mode_ || !time_mode_ || target_time_seconds_ <= 0.0f) return;
    if (displayed_progress_ == percent) return;
    displayed_progress_ = percent;
    char text[16];
    std::snprintf(text, sizeof(text), "%.1fs", (percent / 100.0f) * target_time_seconds_);
    lv_label_set_text(weight_label_, text);
}

void GrindingScreenBeanFlow::set_completed(bool completed) {
    completed_ = completed;
    lv_obj_set_style_text_color(weight_label_,
                                lv_color_hex(completed ? FLOW_COMPLETE_COLOR : THEME_COLOR_TEXT_PRIMARY), 0);
    if (completed) set_activity(GrindScreenActivity::STOPPED);
}

void GrindingScreenBeanFlow::set_time_mode(bool enabled) {
    time_mode_ = enabled;
}

void GrindingScreenBeanFlow::set_manual_mode(bool enabled) {
    manual_mode_ = enabled;
    update_target_visibility();
}

void GrindingScreenBeanFlow::set_activity(GrindScreenActivity activity) {
    activity_ = activity;
    if (activity_ == GrindScreenActivity::ACTIVE) {
        flow_velocity_ = 13.0f;
        set_particles_visible(true);
    } else if (activity_ == GrindScreenActivity::PULSE) {
        flow_velocity_ = 24.0f;
        set_particles_visible(true);
    } else if (activity_ == GrindScreenActivity::STOPPED) {
        flow_velocity_ = 0.0f;
        set_particles_visible(false);
    }
    resume_motion_if_needed();
}

void GrindingScreenBeanFlow::motion_timer_cb(lv_timer_t* timer) {
    auto* screen = static_cast<GrindingScreenBeanFlow*>(lv_timer_get_user_data(timer));
    if (screen) screen->update_motion();
}

void GrindingScreenBeanFlow::update_motion() {
    if (!visible_) return;
    if (activity_ == GrindScreenActivity::SETTLING) {
        flow_velocity_ *= 0.72f;
        if (flow_velocity_ < 0.4f) {
            flow_velocity_ = 0.0f;
            set_particles_visible(false);
            if (motion_timer_) lv_timer_pause(motion_timer_);
            return;
        }
    }
    if (flow_velocity_ <= 0.0f) return;

    particle_progress_ = (particle_progress_ + static_cast<int>(flow_velocity_)) % 100;
    const int active_particles = activity_ == GrindScreenActivity::PULSE ? PARTICLE_COUNT : 5;
    for (int i = 0; i < PARTICLE_COUNT; ++i) {
        if (i >= active_particles) {
            lv_obj_add_flag(particles_[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        const int progress = (particle_progress_ + i * 19) % 100;
        const int x = 137 + ((i % 3) - 1) * 8 + (progress < 50 ? progress / 12 : (100 - progress) / 12);
        const int y = PARTICLE_START_Y + (progress * PARTICLE_TRAVEL_PX) / 100;
        lv_obj_set_pos(particles_[i], x, y);
        lv_obj_clear_flag(particles_[i], LV_OBJ_FLAG_HIDDEN);
    }
}

void GrindingScreenBeanFlow::set_particles_visible(bool visible) {
    for (auto* particle : particles_) {
        if (!particle) continue;
        if (visible) {
            lv_obj_clear_flag(particle, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(particle, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void GrindingScreenBeanFlow::update_target_visibility() {
    if (!target_label_) return;
    if (manual_mode_) {
        lv_obj_add_flag(target_label_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(target_label_, LV_OBJ_FLAG_HIDDEN);
    }
}

void GrindingScreenBeanFlow::resume_motion_if_needed() {
    if (!visible_ || !motion_timer_) return;
    if (activity_ == GrindScreenActivity::ACTIVE || activity_ == GrindScreenActivity::PULSE ||
        (activity_ == GrindScreenActivity::SETTLING && flow_velocity_ > 0.0f)) {
        lv_timer_resume(motion_timer_);
    } else {
        lv_timer_pause(motion_timer_);
    }
}
