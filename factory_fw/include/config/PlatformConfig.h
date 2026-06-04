#pragma once
#include <nlohmann/json.hpp>
#include <string>

namespace ft {

class PlatformConfig {
public:
    static PlatformConfig& instance();

    bool loadFromFile(const std::string& path);
    bool load(const nlohmann::json& j);

    std::string platformName() const;
    const nlohmann::json& raw() const { return root_; }

private:
    nlohmann::json root_;
    bool loaded_ = false;
};

} // namespace ft
