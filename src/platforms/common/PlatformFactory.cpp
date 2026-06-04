#include "platforms/common/interface/IPlatform.h"
#include <unordered_map>
#include <string>

namespace ft {

static std::unordered_map<std::string, PlatformFactoryFunc>& platformRegistry() {
    static std::unordered_map<std::string, PlatformFactoryFunc> reg;
    return reg;
}

bool registerPlatformFactory(const char* name, PlatformFactoryFunc factory) {
    platformRegistry()[name] = factory;
    return true;
}

std::unique_ptr<IPlatform> createPlatform(const char* name) {
    auto it = platformRegistry().find(name);
    if (it != platformRegistry().end()) {
        return it->second();
    }
    return nullptr;
}

// NullPlatform — defined in the same TU as PlatformFactory to prevent
// static-library dead-strip from discarding the self-registration symbol.
class NullPlatform : public IPlatform {
public:
    bool init(const nlohmann::json&) override { return true; }
    const char* name() const override { return "null"; }
    void registerDrivers(DriverRegistry&) override {}
};

std::unique_ptr<IPlatform> createNullPlatform() {
    return std::make_unique<NullPlatform>();
}

static bool _reg_null = registerPlatformFactory("null", createNullPlatform);

} // namespace ft
