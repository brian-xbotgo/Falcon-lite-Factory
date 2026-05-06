// Key test: 17R / 20R
// 17R: board-level, any 2 of 6 keys → pass
// 20R: system-level, all 6 keys required, real-time responses per key
//
// Hardware: 3 input devices (adc-m-keys / adc-ab-keys / gpio-keys)
//           6 physical keys: gpio-power(0x290), F13, F14, F15, F16, F17
// error_code: 6-bit mask, 1=not yet pressed
//   bit0: gpio-power (0x290)
//   bit1: KEY_F13 (183)
//   bit2: KEY_F14 (184)
//   bit3: KEY_F15 (185)
//   bit4: KEY_F16 (186)
//   bit5: KEY_F17 (187)

#include "tests/KeyTest.h"
#include "core/TestEngine.h"
#include "common/Types.h"
#include <linux/input.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/select.h>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace ft {

static constexpr int TOTAL_KEYS      = 6;
static constexpr int NUM_DEVS        = 3;
static constexpr int TIMEOUT_SECONDS = 10;

static const char* DEV_PATHS[NUM_DEVS] = {
    "/dev/input/event0",   // adc-m-keys:  F15, F16, F17
    "/dev/input/event1",   // adc-ab-keys: F13, F14
    "/dev/input/event2",   // gpio-keys:   power (0x290)
};

// Key codes in order: bit0..bit5
static const int key_codes[TOTAL_KEYS] = {
    0x0290,   // gpio power key
    KEY_F13,  // 183
    KEY_F14,  // 184
    KEY_F15,  // 185
    KEY_F16,  // 186
    KEY_F17,  // 187
};

static uint32_t do_button_test(const ProtoHeader& hdr, bool pcba_mode,
                               TestEngine* engine, const std::string& resp_topic) {
    uint32_t error_code = 0b111111;       // all 6 bits = 1 (not pressed)
    int key_detected[TOTAL_KEYS] = {};

    // Open all 3 input devices
    int fd[NUM_DEVS];
    int max_fd = 0;
    for (int i = 0; i < NUM_DEVS; ++i) {
        fd[i] = open(DEV_PATHS[i], O_RDONLY | O_NONBLOCK);
        if (fd[i] < 0) {
            std::fprintf(stderr, "[button] failed to open %s\n", DEV_PATHS[i]);
            continue;
        }
        if (fd[i] > max_fd) max_fd = fd[i];
    }

    auto is_two_or_more = [&]() -> bool {
        int count = 0;
        for (int i = 0; i < TOTAL_KEYS; ++i)
            if (key_detected[i]) { count++; if (count >= 2) return true; }
        return false;
    };
    auto is_all = [&]() -> bool {
        for (int i = 0; i < TOTAL_KEYS; ++i)
            if (!key_detected[i]) return false;
        return true;
    };

    struct input_event event;
    time_t start_time = time(nullptr);

    while (true) {
        if (time(nullptr) - start_time > TIMEOUT_SECONDS) {
            std::fprintf(stderr, "[button] timeout %ds\n", TIMEOUT_SECONDS);
            break;
        }

        if (pcba_mode && is_two_or_more()) break;
        if (is_all()) break;

        fd_set read_fds;
        FD_ZERO(&read_fds);
        for (int i = 0; i < NUM_DEVS; ++i)
            if (fd[i] >= 0) FD_SET(fd[i], &read_fds);

        struct timeval tv = {1, 0};
        int ret = select(max_fd + 1, &read_fds, nullptr, nullptr, &tv);
        if (ret < 0) break;
        if (ret == 0) continue;

        for (int d = 0; d < NUM_DEVS; ++d) {
            if (fd[d] < 0 || !FD_ISSET(fd[d], &read_fds)) continue;

            int bytes = read(fd[d], &event, sizeof(struct input_event));
            if (bytes != sizeof(struct input_event)) continue;

            if (event.type == EV_KEY && event.value == 1) {
                for (int k = 0; k < TOTAL_KEYS; ++k) {
                    if (event.code == key_codes[k] && !key_detected[k]) {
                        error_code &= ~(1u << k);
                        key_detected[k] = 1;
                        std::fprintf(stderr, "[button] key 0x%03x pressed, err=0x%02X\n",
                                     event.code, error_code);

                        if (!pcba_mode && engine && !resp_topic.empty()) {
                            std::string resp = hdr.build_response(error_code);
                            engine->publish(resp_topic, resp, 2);
                        }
                        break;
                    }
                }
            }
        }

        usleep(10000);
    }

    for (int i = 0; i < NUM_DEVS; ++i)
        if (fd[i] >= 0) close(fd[i]);

    std::fprintf(stderr, "[button] done, err=0x%08X\n", error_code);
    return error_code;
}

void register_key_tests(TestEngine& engine) {
    engine.registerTest("17R", [](const ProtoHeader& hdr) -> uint32_t {
        return do_button_test(hdr, true, nullptr, "");
    }, /*async=*/true);

    engine.registerRaw("20R", [&engine](const uint8_t* data, size_t len) {
        ProtoHeader hdr;
        if (!hdr.parse(data, len)) return;
        save_sn(hdr.sn);

        uint32_t err = do_button_test(hdr, false, &engine, "20A");
        std::string resp = hdr.build_response(err);
        engine.publish("20A", resp, 2);
    }, /*async=*/true);
}

} // namespace ft
