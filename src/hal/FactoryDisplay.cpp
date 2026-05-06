#include "hal/FactoryDisplay.h"
#include "hal/GpioController.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>

extern "C" {
int fbdev_init(void);
void fbdev_exit(void);
void fbdev_flush(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_p);
}

namespace ft {

// ---- Display constants (matching original lcd_info.hpp) ----
static constexpr int DISP_W = 428;
static constexpr int DISP_H = 156;
static constexpr uint32_t BG_COLOR = 0x000000;

static constexpr const char* FONT_PATH  = "D:Rajdhani/Rajdhani-SemiBold-5.ttf";
static constexpr int FONT_LABEL_SIZE    = 48;   // battery %, status, model, KEY
static constexpr int FONT_TIMER_SIZE    = 52;   // HH:MM:SS

// ---- Layout positions (matching original FACTORY_MODE placements) ----
// Row 1 — aging status
static constexpr int STATUS_X = 152;
static constexpr int STATUS_Y = 20;

// Row 2 — KEY / battery% / model
static constexpr int KEY_X     = 10;
static constexpr int BAT_X     = 152;
static constexpr int MODEL_X   = 310;
static constexpr int ROW2_Y    = 60;

// Row 3 — timer (shoot position from original)
static constexpr int TIMER_X   = 140;
static constexpr int TIMER_Y   = 100;

// ---- Color-cycling table (blue→green→red→white→black, 1 s each) ----
static constexpr uint32_t CYCLE_COLORS[] = {
    0x0000FF, 0x00FF00, 0xFF0000, 0xFFFFFF, 0x000000,
};
static constexpr int CYCLE_COUNT = sizeof(CYCLE_COLORS) / sizeof(CYCLE_COLORS[0]);
static int cycle_index = 0;

// ---- Tiny helpers ----

static std::string read_file(const char* path) {
    std::string out;
    FILE* f = fopen(path, "r");
    if (!f) return out;
    char buf[256];
    while (fgets(buf, sizeof(buf), f)) out += buf;
    fclose(f);
    return out;
}

static void write_file(const char* path, const char* fmt, int val) {
    char buf[128];
    int n = snprintf(buf, sizeof(buf), fmt, val);
    int fd = open(path, O_WRONLY | O_SYNC);
    if (fd < 0) return;
    ::write(fd, buf, n);
    close(fd);
}

// ---- Backlight (GPIO 92) ----
static void backlight_on()  { write_file("/sys/class/gpio/gpio92/value", "%d", 1); }
static void backlight_off() { write_file("/sys/class/gpio/gpio92/value", "%d", 0); }

static void backlight_init() {
    GpioController::exportGpio(92);
    GpioController::setDirection(92, "out");
    backlight_on();
}

// ---- LVGL flush callback ----
static void disp_flush(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_p) {
    fbdev_flush(drv, area, color_p);
}

// ===================================================================
//  Timer callbacks
// ===================================================================

void FactoryDisplay::bg_timer_cb(lv_timer_t* /*t*/) {
    cycle_index = (cycle_index + 1) % CYCLE_COUNT;
    uint32_t c = CYCLE_COLORS[cycle_index];
    lv_obj_set_style_bg_color(lv_scr_act(),
        lv_color_make((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF), 0);
}

void FactoryDisplay::update_timer_cb(lv_timer_t* t) {
    auto* self = static_cast<FactoryDisplay*>(t->user_data);
    if (!self) return;

    // ---- battery model (read once until found) ----
    static bool model_read = false;
    if (!model_read && self->model_label_) {
        std::string m = read_file("/sys/class/power_supply/cw221X-bat/model_name");
        if (!m.empty()) {
            while (!m.empty() && (m.back() == '\n' || m.back() == '\r')) m.pop_back();
            if (!m.empty()) { lv_label_set_text(self->model_label_, m.c_str()); model_read = true; }
        }
    }

    // ---- key-valid flag ----
    static bool key_checked = false;
    if (!key_checked && access("/userdata/key_valid", F_OK) == 0) {
        key_checked = true;
        if (self->key_label_) lv_label_set_text(self->key_label_, "KEY");
    }

    // ---- aging state machine ----
    static int      last_state     = -1;
    static uint64_t aging_start_ms = 0;
    static bool     timer_running  = false;

    std::string content = read_file("/userdata/aging_completed.txt");
    if (content.empty()) return;

    int state = content[0] - '0';
    if (state < 0 || state > 4) return;

    if (state != last_state) {
        last_state = state;

        switch (state) {
        case 0:  // idle
            lv_label_set_text(self->status_label_, "");
            break;

        case 1:  // finished
            timer_running = false;
            lv_label_set_text(self->status_label_, "FINISHED");
            break;

        case 2:  // aging started
            {
                aging_start_ms = ({ struct timeval tv; gettimeofday(&tv, nullptr);
                                    (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000; });
                timer_running = true;

                std::string conf = read_file("/userdata/aging_time.conf");
                if (!conf.empty()) {
                    while (!conf.empty() && (conf.back() == '\n' || conf.back() == '\r'))
                        conf.pop_back();
                }
                char text[64];
                if (conf.empty())      snprintf(text, sizeof(text), "AGING UNDEFINED");
                else if (conf == "inf") snprintf(text, sizeof(text), "AGING INF");
                else                    snprintf(text, sizeof(text), "AGING %s H", conf.c_str());
                lv_label_set_text(self->status_label_, text);
            }
            break;

        case 3:  // failed
            timer_running = false;
            {
                int reason = (content.length() > 1) ? content[1] - '0' : 0;
                const char* s;
                switch (reason) {
                case 0: s = "FAIL";             break;
                case 1: s = "FAIL-LOW_BAT";     break;
                case 2: s = "FAIL-WIFI";        break;
                case 3: s = "FAIL-HALL_VOLT";   break;
                case 4: s = "FAIL-CAM";         break;
                case 5: s = "FAIL-HALL_INIT";   break;
                default:s = "FAIL";             break;
                }
                lv_label_set_text(self->status_label_, s);
            }
            break;

        case 4:  // aborted
            timer_running = false;
            lv_label_set_text(self->status_label_, "AGING ABORT");
            break;
        }
    }

    // ---- elapsed timer ----
    if (timer_running && self->timer_label_) {
        uint64_t now = ({ struct timeval tv; gettimeofday(&tv, nullptr);
                          (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000; });
        int total = (int)((now - aging_start_ms) / 1000);
        int h = total / 3600;
        int m = (total % 3600) / 60;
        int s = total % 60;
        char buf[16];
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d", h, m, s);
        lv_label_set_text(self->timer_label_, buf);
    }
}

// ===================================================================
//  Public API
// ===================================================================

FactoryDisplay& FactoryDisplay::instance() {
    static FactoryDisplay fd;
    return fd;
}

bool FactoryDisplay::init() {
    if (initialized_) return true;

    lv_init();

    // ---- Framebuffer ----
    if (fbdev_init() != 0) {
        std::fprintf(stderr, "[FactoryDisplay] fbdev_init failed\n");
        return false;
    }

    static lv_color_t buf1[DISP_W * DISP_H];
    static lv_color_t buf2[DISP_W * DISP_H];
    static lv_disp_draw_buf_t draw_buf;
    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, DISP_W * DISP_H);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res  = DISP_W;
    disp_drv.ver_res  = DISP_H;
    disp_drv.flush_cb = disp_flush;
    disp_drv.draw_buf = &draw_buf;
    disp_drv.full_refresh = 1;
    lv_disp_drv_register(&disp_drv);

    // ---- Screen style ----
    static lv_style_t scr_style;
    lv_style_init(&scr_style);
    lv_style_set_bg_color(&scr_style, lv_color_hex(BG_COLOR));
    lv_style_set_border_color(&scr_style, lv_color_hex(BG_COLOR));
    lv_obj_add_style(lv_scr_act(), &scr_style, 0);
    lv_obj_set_scrollbar_mode(lv_scr_act(), LV_SCROLLBAR_MODE_OFF);

    // ---- Load TTF fonts (same font file as original, two sizes) ----
    label_font_ = lv_tiny_ttf_create_file(FONT_PATH, FONT_LABEL_SIZE);
    timer_font_ = lv_tiny_ttf_create_file(FONT_PATH, FONT_TIMER_SIZE);

    if (!label_font_ || !timer_font_) {
        std::fprintf(stderr, "[FactoryDisplay] TTF font load failed (path=%s)\n", FONT_PATH);
        // Continue anyway — labels will use the default font.
    }

    // ---- Row 1: Aging status (y=20, x=152) ----
    status_label_ = lv_label_create(lv_scr_act());
    lv_obj_set_pos(status_label_, STATUS_X, STATUS_Y);
    lv_obj_set_style_text_color(status_label_, lv_color_make(0xFF, 0xFF, 0xFF), 0);
    if (label_font_) lv_obj_set_style_text_font(status_label_, label_font_, 0);
    lv_label_set_text(status_label_, "");

    // ---- Row 2: KEY (y=60, x=10) ----
    key_label_ = lv_label_create(lv_scr_act());
    lv_obj_set_pos(key_label_, KEY_X, ROW2_Y);
    lv_obj_set_style_text_color(key_label_, lv_color_make(0xFF, 0xFF, 0xFF), 0);
    if (label_font_) lv_obj_set_style_text_font(key_label_, label_font_, 0);
    lv_label_set_text(key_label_, "");

    // ---- Row 2: Battery % (y=60, x=152) ----
    battery_label_ = lv_label_create(lv_scr_act());
    lv_obj_set_pos(battery_label_, BAT_X, ROW2_Y);
    lv_obj_set_style_text_color(battery_label_, lv_color_make(0xFF, 0xFF, 0xFF), 0);
    if (label_font_) lv_obj_set_style_text_font(battery_label_, label_font_, 0);
    lv_label_set_text(battery_label_, " ");

    // ---- Row 2: Battery model (y=60, x=310) ----
    model_label_ = lv_label_create(lv_scr_act());
    lv_obj_set_pos(model_label_, MODEL_X, ROW2_Y);
    lv_obj_set_style_text_color(model_label_, lv_color_make(0xFF, 0xFF, 0xFF), 0);
    if (label_font_) lv_obj_set_style_text_font(model_label_, label_font_, 0);
    lv_label_set_text(model_label_, "NA");

    // ---- Row 3: Timer (y=100, x=140) ----
    timer_label_ = lv_label_create(lv_scr_act());
    lv_obj_set_pos(timer_label_, TIMER_X, TIMER_Y);
    lv_obj_set_style_text_color(timer_label_, lv_color_make(0xFF, 0xFF, 0xFF), 0);
    if (timer_font_) lv_obj_set_style_text_font(timer_label_, timer_font_, 0);
    lv_label_set_text(timer_label_, "00:00:00");

    // ---- Per-second timers ----
    bg_timer_     = lv_timer_create(bg_timer_cb,     1000, nullptr);
    update_timer_ = lv_timer_create(update_timer_cb, 1000, this);

    // ---- Backlight ----
    backlight_init();

    initialized_ = true;
    std::fprintf(stderr, "[FactoryDisplay] initialized %dx%d, font=%s\n", DISP_W, DISP_H, FONT_PATH);
    return true;
}

void FactoryDisplay::deinit() {
    if (!initialized_) return;
    backlight_off();
    fbdev_exit();
    initialized_ = false;
}

uint32_t FactoryDisplay::taskHandler() {
    return lv_task_handler();
}

void FactoryDisplay::setBatteryPercent(int pct) {
    if (battery_label_) lv_label_set_text_fmt(battery_label_, "%d%%", pct);
}

void FactoryDisplay::setBatteryModel(const char* model) {
    if (model_label_) lv_label_set_text(model_label_, model);
}

void FactoryDisplay::setKeyValid(bool valid) {
    if (key_label_) lv_label_set_text(key_label_, valid ? "KEY" : "");
}

} // namespace ft
