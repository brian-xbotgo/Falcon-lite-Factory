#pragma once

#include "platforms/common/interface/IRecorder.h"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace ft::falcon_runtime {

void prepareStartup(const nlohmann::json& config);
void prepareWifiIdentity();
bool ensureRkaiqStarted(const nlohmann::json& config, int timeoutMs);
bool waitRkaiqReady(const RecorderConfig& config, int timeoutMs);

} // namespace ft::falcon_runtime
