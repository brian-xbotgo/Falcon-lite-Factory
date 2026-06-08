#include "platforms/common/interface/IPlatform.h"
#include <string>
#include <unordered_map>

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

// Keep BasePlatform in this translation unit so static-library member selection
// cannot drop the self-registration symbol.
class BasePlatform : public IPlatform {
public:
    bool init(const nlohmann::json&) override { return true; }
    const char* name() const override { return "base"; }
    void registerDrivers(DriverRegistry&) override {}
};

std::unique_ptr<IPlatform> createBasePlatform() {
    return std::make_unique<BasePlatform>();
}

static bool _reg_base = registerPlatformFactory("base", createBasePlatform);

} // namespace ft