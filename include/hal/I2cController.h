#pragma once
#include <string>

namespace ft {

class I2cController {
public:
    static bool writeByte(int bus, int addr, uint8_t reg, uint8_t value);
    static bool readByte(int bus, int addr, uint8_t reg, uint8_t& value);
    static bool probe(int bus, int addr);
};

} // namespace ft
