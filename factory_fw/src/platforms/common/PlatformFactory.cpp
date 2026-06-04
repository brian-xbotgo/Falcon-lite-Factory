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

} // namespace ft
