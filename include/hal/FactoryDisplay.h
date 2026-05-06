#pragma once

#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif
#include "lvgl.h"
#ifdef __cplusplus
}
#endif

namespace ft {

// LVGL factory test display, decoupled from main firmware charge_lvgl_app.
// Uses the same TTF font (Rajdhani-SemiBold) as the original so the
// rendered output matches what factory operators expect.
class FactoryDisplay {
public:
    static FactoryDisplay& instance();

    bool init();
    void deinit();
    bool isInitialized() const { return initialized_; }

    // Call each main-loop iteration; returns ms until lvgl next needs attention.
    uint32_t taskHandler();

    void setBatteryPercent(int pct);
    void setBatteryModel(const char* model);
    void setKeyValid(bool valid);

private:
    FactoryDisplay() = default;
    ~FactoryDisplay() { deinit(); }

    bool initialized_ = false;

    // TTF font — loaded once, shared by all labels.
    lv_font_t* label_font_ = nullptr;   // 48 pt
    lv_font_t* timer_font_ = nullptr;   // 52 pt

    lv_obj_t* status_label_   = nullptr;   // "AGING 2 H" / "FINISHED" / "FAIL-xxx"
    lv_obj_t* battery_label_  = nullptr;   // "85%"
    lv_obj_t* model_label_    = nullptr;   // battery model name
    lv_obj_t* key_label_      = nullptr;   // "KEY" when key-valid file exists
    lv_obj_t* timer_label_    = nullptr;   // "00:00:00"

    lv_timer_t* bg_timer_     = nullptr;
    lv_timer_t* update_timer_ = nullptr;

    static void bg_timer_cb(lv_timer_t* t);
    static void update_timer_cb(lv_timer_t* t);
};

} // namespace ft
