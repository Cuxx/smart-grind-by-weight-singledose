#pragma once

#include <lvgl.h>

#include "../../config/constants.h"
#include "grinding_screen_base.h"

class GrindingScreenBeanFlow : public IGrindingScreen {
private:
    static constexpr uint32_t TIMER_PERIOD_MS = 50;
    static constexpr int PARTICLE_COUNT = 8;

    lv_obj_t* screen_ = nullptr;
    lv_obj_t* profile_label_ = nullptr;
    lv_obj_t* target_label_ = nullptr;
    lv_obj_t* weight_label_ = nullptr;
    lv_obj_t* particles_[PARTICLE_COUNT] = {};
    lv_timer_t* motion_timer_ = nullptr;
    bool visible_ = false;
    bool time_mode_ = false;
    bool manual_mode_ = false;
    bool completed_ = false;
    GrindScreenActivity activity_ = GrindScreenActivity::STOPPED;
    float target_time_seconds_ = 0.0f;
    float flow_velocity_ = 0.0f;
    int particle_progress_ = 0;
    int displayed_progress_ = -1;
    char displayed_weight_text_[16] = "";

    static void motion_timer_cb(lv_timer_t* timer);
    void update_motion();
    void set_particles_visible(bool visible);
    void update_target_visibility();
    void resume_motion_if_needed();

public:
    ~GrindingScreenBeanFlow() override;

    void create() override;
    void show() override;
    void hide() override;
    void update_profile_name(const char* name) override;
    void update_target_weight(float weight) override;
    void update_target_weight_text(const char* text) override;
    void update_target_time(float seconds) override;
    void update_current_weight(float weight) override;
    void update_tare_display() override;
    void update_progress(int percent) override;
    void set_completed(bool completed) override;
    void set_time_mode(bool enabled) override;
    void set_manual_mode(bool enabled) override;
    void set_activity(GrindScreenActivity activity) override;

    bool is_visible() const override { return visible_; }
    lv_obj_t* get_screen() const override { return screen_; }
};
