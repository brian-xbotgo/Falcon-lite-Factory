#pragma once

#include <nlohmann/json.hpp>

namespace ft {

enum class StatusLedMode {
    Off,
    Normal,
    Test,
    TestDone,
    TestFail
};

class StatusLedController {
public:
    static StatusLedController& instance();

    bool configure(const nlohmann::json& platformConfig);
    bool start(StatusLedMode mode);
    bool setMode(StatusLedMode mode);
    void stop();
    bool isConfigured() const;

private:
    StatusLedController() = default;
};

const char* statusLedModeName(StatusLedMode mode);

} // namespace ft
