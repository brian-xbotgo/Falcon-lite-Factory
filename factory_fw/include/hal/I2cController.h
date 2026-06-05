#pragma once
#include <string>

namespace ft {

class I2cController {
public:
    static bool writeByte(int bus, int addr, uint8_t reg, uint8_t value);
    static bool readByte(int bus, int addr, uint8_t reg, uint8_t& value);
    static bool probe(int bus, int addr);

    // 16-bit register read/write (shell-based, compatible with old codebase)
    static int  readRegister(int bus, int addr, int reg);
    static bool writeRegister(int bus, int addr, int reg, uint8_t value);
};

} // namespace ft
