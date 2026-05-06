// Battery test: 15R / 24R
// Reads /sys/class/power_supply/cw221X-bat/uevent
// error_code: bit0=voltage<3.6V, bit1=read failed

#include "tests/BatteryTest.h"
#include "core/TestEngine.h"
#include "common/Types.h"
#include "common/ShellUtils.h"
#include <cstdio>
#include <cstdlib>

namespace ft {

static uint32_t do_battery_test(const ProtoHeader& hdr) {
    uint32_t err = 0;

    std::string content = read_file("/sys/class/power_supply/cw221X-bat/uevent");
    if (content.empty()) {
        err |= 0x0002;
        std::fprintf(stderr, "[battery] cannot read uevent\n");
        return err;
    }

    std::string voltage_str = uevent_get(content, "POWER_SUPPLY_VOLTAGE_NOW");
    if (voltage_str.empty()) {
        err |= 0x0002;
        return err;
    }

    long voltage_uv = std::atol(voltage_str.c_str());
    double voltage_v = voltage_uv / 1000000.0;

    if (voltage_v < 3.6) {
        err |= 0x0001;
    }

    std::fprintf(stderr, "[battery] voltage=%.3fV, err=0x%08X\n", voltage_v, err);
    return err;
}

void register_battery_tests(TestEngine& engine) {
    engine.registerTest("15R", do_battery_test);
    engine.registerTest("24R", do_battery_test);
}

} // namespace ft
