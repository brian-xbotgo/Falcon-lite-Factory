#pragma once
#include <nlohmann/json.hpp>

namespace ft {
class DriverRegistry;

class IPlatform {
public:
    virtual ~IPlatform() = default;
    virtual bool init(const nlohmann::json& config) = 0;
    virtual const char* name() const = 0;
    virtual void registerDrivers(DriverRegistry& reg) = 0;

    virtual const char* gpuTestPath() const    { return nullptr; }
    virtual const char* npuTestPath() const    { return nullptr; }
    virtual const char* stressTestPath() const { return nullptr; }
    virtual const char* emmcTestPath() const   { return nullptr; }
};

using PlatformFactoryFunc = std::unique_ptr<IPlatform>(*)();
bool registerPlatformFactory(const char* name, PlatformFactoryFunc factory);
std::unique_ptr<IPlatform> createPlatform(const char* name);

} // namespace ft
