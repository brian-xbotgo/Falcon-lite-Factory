#pragma once

namespace ft {

class IHallDriver {
public:
    virtual ~IHallDriver() = default;
    virtual bool init() = 0;
    virtual void deinit() = 0;
    virtual float readValue() = 0;
    virtual bool isInitialized() const = 0;
};

} // namespace ft
