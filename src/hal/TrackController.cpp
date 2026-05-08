// Tracking HAL — single-process interface to multi_media action_tracking.
// Default weak implementations no-op; multi_media overrides when linked.

#include "hal/TrackController.h"
#include <cstdio>

namespace ft {

__attribute__((weak))
int track_hal_init(void* ctx) {
    std::fprintf(stderr, "[track] no multi_media linked, init skipped\n");
    return 0;
}

__attribute__((weak))
int track_hal_start(void) {
    std::fprintf(stderr, "[track] no multi_media linked, start skipped\n");
    return 0;
}

__attribute__((weak))
int track_hal_stop(void) {
    std::fprintf(stderr, "[track] no multi_media linked, stop skipped\n");
    return 0;
}

__attribute__((weak))
void track_hal_deinit(void) {
    std::fprintf(stderr, "[track] no multi_media linked, deinit skipped\n");
}

TrackController& TrackController::instance() {
    static TrackController c;
    return c;
}

void TrackController::init(void* ctx) {
    track_hal_init(ctx);
}

void TrackController::start() {
    track_hal_start();
}

void TrackController::stop() {
    track_hal_stop();
}

void TrackController::deinit() {
    track_hal_deinit();
}

} // namespace ft
