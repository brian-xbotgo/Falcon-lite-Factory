// System commands: 26R (lock detect), 31R (heartbeat), 50R (shutdown)
// 26R: GPIO 98 interrupt detect, 2+ toggles within 20s
// 31R: Echo back payload for connectivity check
// 50R: Execute shutdown script

#include "tests/SysTest.h"
#include "core/TestEngine.h"
#include "common/Types.h"
#include "common/ShellUtils.h"
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <ctime>

namespace ft {

void register_sys_tests(TestEngine& engine) {

    // 26R: Lock/toggle test (GPIO 98 interrupt, 2+ toggles in 20s)
    engine.registerTest("26R", [](const ProtoHeader& hdr) -> uint32_t {
        uint32_t err = 0;
        constexpr int GPIO_NUM = 98;
        char path[128];

        shell_exec("echo 98 > /sys/class/gpio/export 2>/dev/null");
        shell_exec("echo in > /sys/class/gpio/gpio98/direction");
        shell_exec("echo both > /sys/class/gpio/gpio98/edge");

        std::snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", GPIO_NUM);
        int fd = open(path, O_RDONLY);
        if (fd < 0) {
            std::fprintf(stderr, "[26R] cannot open gpio value\n");
            return 0x0001;
        }

        char buf[8];
        lseek(fd, 0, SEEK_SET);
        read(fd, buf, sizeof(buf));

        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLPRI | POLLERR;

        int count = 0;
        time_t start = time(nullptr);

        std::fprintf(stderr, "[26R] waiting for 2 lock toggles in 20s...\n");

        while (true) {
            int ret = poll(&pfd, 1, 1000);
            if (ret < 0) break;

            if (ret == 0) {
                if (time(nullptr) - start >= 20) {
                    err |= 0x0001;
                    std::fprintf(stderr, "[26R] timeout\n");
                    break;
                }
                continue;
            }

            if (pfd.revents & POLLPRI) {
                lseek(fd, 0, SEEK_SET);
                if (read(fd, buf, sizeof(buf)) > 0) {
                    count++;
                    std::fprintf(stderr, "[26R] toggle %d\n", count);
                    if (count >= 2) break;
                }
            }
        }

        close(fd);
        std::fprintf(stderr, "[26R] done, count=%d, err=0x%08X\n", count, err);
        return err;
    }, /*async=*/true);

    // 31R: Heartbeat (echo back payload)
    engine.registerRaw("31R", [&engine](const uint8_t* data, size_t len) {
        std::string payload(reinterpret_cast<const char*>(data), len);
        engine.publish("31A", payload, 2);
    });

    // 50R: Shutdown
    engine.registerRaw("50R", [](const uint8_t*, size_t) {
        std::fprintf(stderr, "[sys] shutdown requested\n");
        std::system("timeout 30s /bin/bash /oem/usr/scripts/power_off.sh");
    });
}

} // namespace ft
