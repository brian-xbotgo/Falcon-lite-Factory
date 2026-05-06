#include "hal/I2cController.h"
#include <cstdio>
#include <cstdlib>
#include <array>

namespace ft {

std::string I2cController::executeI2cCommand(const std::string& cmd) {
    std::array<char, 128> buffer;
    std::string result;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "";
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        result += buffer.data();
    }
    pclose(pipe);
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) {
        result.pop_back();
    }
    return result;
}

int I2cController::readRegister(int bus, int addr, int reg) {
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "i2cget -f -y %d 0x%02x 0x%02x", bus, addr, reg);
    std::string output = executeI2cCommand(cmd);
    if (output.empty()) return -1;
    int value = -1;
    if (output.size() >= 4 && output.substr(0, 2) == "0x") {
        value = static_cast<int>(strtol(output.c_str(), nullptr, 16));
    }
    return value;
}

bool I2cController::writeRegister(int bus, int addr, int reg, uint8_t value) {
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "i2cset -f -y %d 0x%02x 0x%02x 0x%02x b", bus, addr, reg, value);
    return system(cmd) == 0;
}

} // namespace ft
