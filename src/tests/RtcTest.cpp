// RTC + IC test: 10R
// RTC time read/compare, charger IC probe, fuel gauge IC probe
// error_code: bit0=open fail, bit1=read fail, bit2=year<2026, bit3=date fail,
//             bit4=conv fail, bit5=cmp fail, bit6=cmd fail, bit7=charger IC fail,
//             bit9=fuel gauge IC fail, bit10=gauge cmd fail, bit11=dev fail

#include "tests/RtcTest.h"
#include "core/TestEngine.h"
#include "common/Types.h"
#include "common/ShellUtils.h"
#include <ctime>
#include <cstring>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/rtc.h>

namespace ft {

void register_rtc_tests(TestEngine& engine) {
    engine.registerTest("10R", [](const ProtoHeader& hdr) -> uint32_t {
        uint32_t err = 0;

        // ---- RTC test ----
        struct rtc_time rtc_tm = {};
        {
            int fd = open("/dev/rtc", O_RDONLY);
            if (fd < 0) {
                err |= (1 << 0);
                std::fprintf(stderr, "[10R] FAIL: RTC device not found\n");
            } else {
                if (ioctl(fd, RTC_RD_TIME, &rtc_tm) < 0) {
                    err |= (1 << 1);
                    std::fprintf(stderr, "[10R] FAIL: Cannot read RTC time\n");
                }
                close(fd);
            }
        }

        if (rtc_tm.tm_year + 1900 < 2026) {
            err |= (1 << 2);
            std::fprintf(stderr, "[10R] FAIL: RTC year %d < 2026\n", rtc_tm.tm_year + 1900);
        }

        time_t sys_time = time(nullptr);
        if (sys_time == (time_t)-1) {
            err |= (1 << 3);
        }

        struct tm* tm = localtime(&sys_time);
        if (!tm) {
            err |= (1 << 4);
        }

        if (tm && !(err & 0x03)) {
            if (rtc_tm.tm_year != tm->tm_year ||
                rtc_tm.tm_mon  != tm->tm_mon  ||
                rtc_tm.tm_mday != tm->tm_mday ||
                rtc_tm.tm_hour != tm->tm_hour) {
                err |= (1 << 5);
                std::fprintf(stderr, "[10R] FAIL: RTC time != system time\n");
            }
        }

        if (tm) {
            std::fprintf(stderr, "[10R] RTC=%04d-%02d-%02d %02d:%02d:%02d  SYS=%04d-%02d-%02d %02d:%02d:%02d\n",
                rtc_tm.tm_year+1900, rtc_tm.tm_mon+1, rtc_tm.tm_mday,
                rtc_tm.tm_hour, rtc_tm.tm_min, rtc_tm.tm_sec,
                tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday,
                tm->tm_hour, tm->tm_min, tm->tm_sec);
        }

        // ---- Charger IC: SC89890, i2c bus 2, addr 0x6a, reg 0x14 (PN field) ----
        {
            auto r = shell_exec("i2cget -f -y 2 0x6a 0x14");
            if (r.exit_code != 0 || r.output.empty()) {
                err |= (1 << 6);
                std::fprintf(stderr, "[10R] FAIL: charger IC command failed\n");
            } else {
                // reg 0x14 bits[3:5] = PN, expected 4 for SC89890
                unsigned long val = std::strtoul(r.output.c_str(), nullptr, 16);
                if (((val >> 3) & 0x7) == 4) {
                    std::fprintf(stderr, "[10R] PASS: charger IC SC89890 (PN=4)\n");
                } else {
                    err |= (1 << 7);
                    std::fprintf(stderr, "[10R] FAIL: charger IC reg=0x%02lX (expect PN=4)\n", val);
                }
            }
        }

        // ---- Fuel gauge IC: CW2215, i2c bus 5, addr 0x64, reg 0x00 (CHIP_ID) ----
        {
            auto r = shell_exec("i2cget -f -y 5 0x64 0x00");
            if (r.exit_code != 0 || r.output.empty()) {
                err |= (1 << 10);
                std::fprintf(stderr, "[10R] FAIL: fuel gauge IC command failed\n");
            } else {
                if (r.output.substr(0, 4) == "0xa0") {
                    std::fprintf(stderr, "[10R] PASS: fuel gauge CW2215 (CHIP_ID=0xa0)\n");
                } else {
                    err |= (1 << 9);
                    std::fprintf(stderr, "[10R] FAIL: fuel gauge IC = %s (expect 0xa0)\n", r.output.c_str());
                }
            }
        }

        if (access("/dev/cw221X-bat", R_OK) != 0) {
            err |= (1 << 11);
            std::fprintf(stderr, "[10R] FAIL: /dev/cw221X-bat not readable\n");
        }

        std::fprintf(stderr, "[10R] done, sn=%s, err=0x%08X\n", hdr.sn, err);
        return err;
    });
}

} // namespace ft
