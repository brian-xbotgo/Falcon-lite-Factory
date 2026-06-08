#include "tests/ITestModule.h"
#include "config/PlatformConfig.h"
#include <linux/input.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace ft {

namespace {

constexpr const char* kKeyValidFlag = "/userdata/key_valid";

struct KeyTarget {
    int code;
    uint32_t bit;
    const char* name;
};

const std::vector<KeyTarget>& keyTargets()
{
    static const std::vector<KeyTarget> targets = {
        {KEY_POWER,  1u << 0, "KEY_POWER"},
        {KEY_F13,    1u << 1, "KEY_F13"},
        {KEY_F14,    1u << 2, "KEY_F14"},
        {KEY_F15,    1u << 3, "KEY_F15"},
        {KEY_F16,    1u << 4, "KEY_F16"},
        {KEY_F17,    1u << 5, "KEY_F17"},
    };
    return targets;
}

int normalizeKeyCode(int code)
{
    if (code == KEY_MACRO1) {
        return KEY_POWER;
    }
    return code;
}

int parseKeyCode(const std::string& value)
{
    static const std::map<std::string, int> names = {
        {"power", KEY_POWER},
        {"KEY_POWER", KEY_POWER},
        {"F13", KEY_F13},
        {"KEY_F13", KEY_F13},
        {"F14", KEY_F14},
        {"KEY_F14", KEY_F14},
        {"F15", KEY_F15},
        {"KEY_F15", KEY_F15},
        {"F16", KEY_F16},
        {"KEY_F16", KEY_F16},
        {"F17", KEY_F17},
        {"KEY_F17", KEY_F17},
        {"MACRO1", KEY_MACRO1},
        {"KEY_MACRO1", KEY_MACRO1},
        {"0x290", KEY_MACRO1},
    };

    auto it = names.find(value);
    if (it != names.end()) {
        return normalizeKeyCode(it->second);
    }

    char* end = nullptr;
    long code = std::strtol(value.c_str(), &end, 0);
    if (end && *end == '\0') {
        return normalizeKeyCode(static_cast<int>(code));
    }
    return -1;
}

std::vector<std::string> inputDevicesFromConfig(TestContext& ctx)
{
    std::vector<std::string> devices;
    std::set<std::string> seen;
    bool hasConfiguredDevices = false;
    try {
        const auto& input = ctx.config().raw().at("input");
        if (input.contains("key_devices") && input.at("key_devices").is_array()) {
            hasConfiguredDevices = true;
            for (const auto& dev : input.at("key_devices")) {
                const auto path = dev.is_string()
                    ? dev.get<std::string>()
                    : dev.value("path", std::string());
                if (!path.empty() && seen.insert(path).second) {
                    devices.push_back(path);
                }
            }
        }
    } catch (...) {
        // Fall back to the FALCON defaults below.
    }

    bool scanAllDevices = false;
    try {
        scanAllDevices = ctx.params().value("scan_all_devices", false);
        const auto& input = ctx.config().raw().at("input");
        scanAllDevices = scanAllDevices || input.value("scan_all_devices", false);
    } catch (...) {
        // Keep the configured-device behavior.
    }

    if (!hasConfiguredDevices || scanAllDevices) {
        for (int i = 0; i < 32; ++i) {
            char path[64];
            std::snprintf(path, sizeof(path), "/dev/input/event%d", i);
            struct stat st {};
            if (::stat(path, &st) == 0 && seen.insert(path).second) {
                devices.emplace_back(path);
            }
        }
    }

    if (devices.empty()) {
        devices = {"/dev/input/event0", "/dev/input/event2"};
    }
    return devices;
}

std::set<int> expectedKeysFromConfig(TestContext& ctx)
{
    std::set<int> expected;
    try {
        const auto& keys = ctx.config().raw().at("input").at("key_codes");
        if (keys.is_array()) {
            for (const auto& key : keys) {
                if (!key.is_string()) {
                    continue;
                }
                const int code = parseKeyCode(key.get<std::string>());
                if (code >= 0) {
                    expected.insert(code);
                }
            }
        }
    } catch (...) {
        // Fall back to the protocol key set below.
    }

    if (expected.empty()) {
        for (const auto& target : keyTargets()) {
            expected.insert(target.code);
        }
    }

    return expected;
}

uint32_t missingMask(const std::set<int>& expected, const std::set<int>& pressed)
{
    uint32_t mask = 0;
    for (const auto& target : keyTargets()) {
        if (expected.count(target.code) && !pressed.count(target.code)) {
            mask |= target.bit;
        }
    }
    return mask;
}

TestResult failWithErrorCode(const std::string& detail, uint32_t errorCode)
{
    auto result = TestResult::fail(detail);
    result.data["error_code"] = errorCode;
    return result;
}

TestResult progressWithErrorCode(uint32_t errorCode, size_t pressedCount)
{
    auto result = TestResult::fail("key progress");
    result.data["error_code"] = errorCode;
    result.data["pressed_count"] = pressedCount;
    result.data["progress"] = true;
    return result;
}

bool testBit(const unsigned long* bits, int bit)
{
    constexpr int kBitsPerLong = static_cast<int>(sizeof(unsigned long) * 8);
    return (bits[bit / kBitsPerLong] & (1UL << (bit % kBitsPerLong))) != 0;
}

void logInputDeviceCaps(int fd, const std::string& path)
{
    char name[128] = {};
    if (::ioctl(fd, EVIOCGNAME(sizeof(name)), name) < 0) {
        std::snprintf(name, sizeof(name), "unknown");
    }

    unsigned long keyBits[(KEY_MAX + (sizeof(unsigned long) * 8)) /
                          (sizeof(unsigned long) * 8)] = {};
    if (::ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keyBits)), keyBits) < 0) {
        std::fprintf(stderr, "[KeyTest] device %s name=%s caps=unavailable\n",
                     path.c_str(), name);
        return;
    }

    std::fprintf(stderr,
                 "[KeyTest] device %s name=%s supports POWER=%d F13=%d F14=%d F15=%d F16=%d F17=%d MACRO1=%d\n",
                 path.c_str(), name,
                 testBit(keyBits, KEY_POWER) ? 1 : 0,
                 testBit(keyBits, KEY_F13) ? 1 : 0,
                 testBit(keyBits, KEY_F14) ? 1 : 0,
                 testBit(keyBits, KEY_F15) ? 1 : 0,
                 testBit(keyBits, KEY_F16) ? 1 : 0,
                 testBit(keyBits, KEY_F17) ? 1 : 0,
                 testBit(keyBits, KEY_MACRO1) ? 1 : 0);
}

void markKeyValid()
{
    int fd = ::open(kKeyValidFlag, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        ::write(fd, "1\n", 2);
        ::close(fd);
    }
}

} // namespace

class KeyTest : public ITestModule {
public:
    TestResult run(TestContext& ctx) override {
        const bool requireAll = ctx.params().value("require_all", false);
        const int requiredCount = requireAll ? 6 : ctx.params().value("required_count", 2);
        const int timeoutMs = ctx.params().value("timeout_ms", 10000);
        const bool publishEachKey = ctx.params().value("publish_each_key", requireAll);

        const auto expected = expectedKeysFromConfig(ctx);
        auto devicePaths = inputDevicesFromConfig(ctx);
        std::fprintf(stderr,
                     "[KeyTest] start required_count=%d timeout_ms=%d devices=%zu expected=%zu\n",
                     requiredCount, timeoutMs, devicePaths.size(), expected.size());

        std::vector<pollfd> fds;
        std::vector<std::string> fdPaths;
        std::vector<int> ownedFds;
        for (const auto& path : devicePaths) {
            int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK);
            if (fd < 0) {
                std::fprintf(stderr, "[KeyTest] open %s failed: %s\n",
                             path.c_str(), std::strerror(errno));
                continue;
            }
            std::fprintf(stderr, "[KeyTest] opened %s fd=%d\n", path.c_str(), fd);
            logInputDeviceCaps(fd, path);
            ownedFds.push_back(fd);
            fds.push_back({fd, POLLIN, 0});
            fdPaths.push_back(path);
        }

        if (fds.empty()) {
            return failWithErrorCode("no input devices", missingMask(expected, {}));
        }

        std::set<int> pressed;
        const auto start = std::chrono::steady_clock::now();

        while (true) {
            const auto now = std::chrono::steady_clock::now();
            const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - start).count();
            int remainMs = timeoutMs - static_cast<int>(elapsedMs);
            if (remainMs <= 0) {
                break;
            }

            const int rc = ::poll(fds.data(), fds.size(), remainMs > 200 ? 200 : remainMs);
            if (rc < 0) {
                if (errno == EINTR) {
                    continue;
                }
                break;
            }

            for (size_t index = 0; index < fds.size(); ++index) {
                auto& pfd = fds[index];
                const auto& path = fdPaths[index];
                if ((pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
                    std::fprintf(stderr, "[KeyTest] poll issue path=%s fd=%d revents=0x%x\n",
                                 path.c_str(), pfd.fd, pfd.revents);
                }
                if ((pfd.revents & POLLIN) == 0) {
                    continue;
                }

                input_event ev {};
                while (::read(pfd.fd, &ev, sizeof(ev)) == sizeof(ev)) {
                    std::fprintf(stderr, "[KeyTest] event path=%s fd=%d type=%u code=%u value=%d\n",
                                 path.c_str(), pfd.fd, ev.type, ev.code, ev.value);
                    if (ev.type != EV_KEY || ev.value != 1) {
                        continue;
                    }
                    const int code = normalizeKeyCode(ev.code);
                    if (expected.count(code)) {
                        const auto inserted = pressed.insert(code).second;
                        std::fprintf(stderr,
                                     "[KeyTest] key %s raw=%d normalized=%d count=%zu\n",
                                     inserted ? "accepted" : "duplicate",
                                     ev.code, code, pressed.size());
                        if (inserted && publishEachKey) {
                            ctx.publishProgress(
                                progressWithErrorCode(missingMask(expected, pressed),
                                                      pressed.size()));
                        }
                    }
                }
            }

            if (static_cast<int>(pressed.size()) >= requiredCount) {
                for (int fd : ownedFds) {
                    ::close(fd);
                }
                auto result = TestResult::pass();
                result.data["pressed_count"] = pressed.size();
                result.data["missing_mask"] = missingMask(expected, pressed);
                result.data["error_code"] = 0;
                markKeyValid();
                return result;
            }
        }

        for (int fd : ownedFds) {
            ::close(fd);
        }

        const uint32_t errorCode = missingMask(expected, pressed);
        return failWithErrorCode("key test timeout", errorCode ? errorCode : 1);
    }
};

REGISTER_TEST_MODULE("key", KeyTest);

} // namespace ft
