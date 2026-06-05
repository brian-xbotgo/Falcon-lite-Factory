#pragma once
#include "platforms/common/interface/IDisplayDriver.h"

// LVGL headers — include paths set in platforms/falcon/CMakeLists.txt
#ifdef __cplusplus
extern "C" {
#endif
#include "lvgl.h"
#ifdef __cplusplus
}
#endif

namespace ft {

class LvglDisplayDriver : public IDisplayDriver {
public:
    bool init() override;
    void deinit() override;
    uint32_t taskHandler() override;
    void setBatteryPercent(int pct) override;
    void setBatteryModel(const char* model) override;
    void setKeyValid(bool valid) override;
    bool isInitialized() const override { return initialized_; }

private:
    static void bgTimerCb(lv_timer_t* t);
    static void updateTimerCb(lv_timer_t* t);

    bool initialized_ = false;

    lv_font_t* label_font_ = nullptr;
    lv_font_t* timer_font_ = nullptr;

    lv_obj_t* status_label_  = nullptr;
    lv_obj_t* battery_label_ = nullptr;
    lv_obj_t* model_label_   = nullptr;
    lv_obj_t* key_label_     = nullptr;
    lv_obj_t* timer_label_   = nullptr;

    lv_timer_t* bg_timer_     = nullptr;
    lv_timer_t* update_timer_ = nullptr;
};

} // namespace ft
