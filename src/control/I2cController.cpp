#include "control/I2cController.h"
#include "common/ShellUtils.h"
#include <cstdio>
#include <string>

namespace ft {

bool I2cController::writeByte(int bus, int addr, uint8_t reg, uint8_t value) {
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "i2cset -f -y %d 0x%02x 0x%02x 0x%02x", bus, addr, reg, value);
    return system(cmd) == 0;
}

bool I2cController::readByte(int bus, int addr, uint8_t reg, uint8_t& value) {
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "i2cget -f -y %d 0x%02x 0x%02x", bus, addr, reg);
    auto result = shell_exec(cmd);
    if (result.empty()) return false;
    try {
        value = static_cast<uint8_t>(std::stoi(result, nullptr, 0));
    } catch (...) {
        return false;
    }
    return true;
}

bool I2cController::probe(int bus, int addr) {
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "i2cdetect -y %d | grep \" %02x \"", bus, addr);
    return !shell_exec(cmd).empty();
}

int I2cController::readRegister(int bus, int addr, int reg) {
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "i2cget -f -y %d 0x%02x 0x%04x w", bus, addr, reg);
    auto result = shell_exec(cmd);
    if (result.empty()) return -1;
    try {
        return std::stoi(result, nullptr, 0);
    } catch (...) {
        return -1;
    }
}

bool I2cController::writeRegister(int bus, int addr, int reg, uint8_t value) {
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "i2cset -f -y %d 0x%02x 0x%04x 0x%02x", bus, addr, reg, value);
    return system(cmd) == 0;
}

} // namespace ft
