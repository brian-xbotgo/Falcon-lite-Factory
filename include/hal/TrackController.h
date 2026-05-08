#pragma once

// Tracking HAL — abstract interface for AI tracking control.
// Uses weak-linked hooks: multi_media overrides when linked in same process.
// Without multi_media, calls are harmless no-ops.

namespace ft {

class TrackController {
public:
    static TrackController& instance();

    void init(void* ctx = nullptr);
    void start();
    void stop();
    void deinit();

private:
    TrackController() = default;
};

} // namespace ft
