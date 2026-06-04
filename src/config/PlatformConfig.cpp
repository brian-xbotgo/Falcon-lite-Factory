#include "config/PlatformConfig.h"
#include <fstream>

namespace ft {

PlatformConfig& PlatformConfig::instance() {
    static PlatformConfig inst;
    return inst;
}

bool PlatformConfig::loadFromFile(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return false;
    try {
        f >> root_;
        loaded_ = true;
        return true;
    } catch (...) {
        return false;
    }
}

bool PlatformConfig::load(const nlohmann::json& j) {
    root_ = j;
    loaded_ = true;
    return true;
}

std::string PlatformConfig::platformName() const {
    return root_.value("platform", "unknown");
}

} // namespace ft
