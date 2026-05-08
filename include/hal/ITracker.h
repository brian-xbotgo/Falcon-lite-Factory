#pragma once

// Abstract interface for AI tracking — part of HAL abstraction layer.
// Aging test calls this; real implementation injected at startup.
// Factory firmware: NullTracker (no-op).  Official firmware: MediaTracker.

namespace ft {

class ITracker {
public:
    virtual ~ITracker() = default;
    virtual bool start() = 0;
    virtual bool stop()  = 0;
    virtual bool isTracking() const = 0;
};

} // namespace ft
