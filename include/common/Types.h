#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <atomic>
#include <mutex>

namespace ft {

// ========= 协议常量 =========
static constexpr size_t PROTO_SN_LEN     = 16;
static constexpr size_t PROTO_BIZID_LEN  = 16;
static constexpr size_t PROTO_MSGID_LEN  = 8;
static constexpr size_t PROTO_HEADER_LEN = PROTO_SN_LEN + PROTO_BIZID_LEN + PROTO_MSGID_LEN;
static constexpr size_t PROTO_RESP_LEN   = PROTO_HEADER_LEN + 4;

inline uint32_t to_big_endian(uint32_t val) {
    uint8_t buf[4];
    buf[0] = (val >> 24) & 0xFF;
    buf[1] = (val >> 16) & 0xFF;
    buf[2] = (val >> 8)  & 0xFF;
    buf[3] = val & 0xFF;
    uint32_t result;
    memcpy(&result, buf, 4);
    return result;
}

inline uint32_t from_big_endian(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
           (uint32_t(p[2]) << 8)  | (uint32_t(p[3]));
}

// ========= 协议头 =========
struct ProtoHeader {
    char sn[PROTO_SN_LEN + 1]       = {};
    char bizid[PROTO_BIZID_LEN + 1] = {};
    char msgid[PROTO_MSGID_LEN + 1] = {};

    bool parse(const uint8_t* data, size_t len) {
        if (len < PROTO_HEADER_LEN) return false;
        memcpy(sn,    data, PROTO_SN_LEN);
        memcpy(bizid, data + PROTO_SN_LEN, PROTO_BIZID_LEN);
        memcpy(msgid, data + PROTO_SN_LEN + PROTO_BIZID_LEN, PROTO_MSGID_LEN);
        return true;
    }

    std::string build_response(uint32_t error_code) const {
        std::string resp(PROTO_RESP_LEN, '\0');
        memcpy(&resp[0], sn, PROTO_SN_LEN);
        memcpy(&resp[PROTO_SN_LEN], bizid, PROTO_BIZID_LEN);
        memcpy(&resp[PROTO_SN_LEN + PROTO_BIZID_LEN], msgid, PROTO_MSGID_LEN);
        uint32_t net_code = to_big_endian(error_code);
        memcpy(&resp[PROTO_HEADER_LEN], &net_code, 4);
        return resp;
    }
};

inline std::string req_to_resp_topic(const std::string& req_topic) {
    if (req_topic.empty() || req_topic.back() != 'R') return "";
    std::string resp = req_topic;
    resp.back() = 'A';
    return resp;
}

inline void save_sn(const char* sn) {
    static char cached_sn[PROTO_SN_LEN + 1] = {};
    if (std::strncmp(cached_sn, sn, PROTO_SN_LEN) == 0 && cached_sn[0] != '\0') return;
    FILE* f = std::fopen("/device_data/pcba.txt", "w");
    if (f) {
        std::fwrite(sn, 1, std::strlen(sn), f);
        std::fclose(f);
        std::strncpy(cached_sn, sn, PROTO_SN_LEN);
    }
}

// ========= LED 模式 =========
enum LedMode {
    MODE_NORMAL    = 0,
    MODE_TEST      = 1,
    MODE_TEST_DONE = 2,
    MODE_TEST_FAIL = 3,
};

// ========= 全局状态 =========
struct Globals {
    std::atomic<bool> aging_test_start{false};
    std::atomic<bool> aging_motor_test_start{false};
    std::atomic<bool> upper_limit_reached{false};
    std::atomic<bool> lower_limit_reached{false};
    std::atomic<int>  led_mode{MODE_NORMAL};
    std::mutex        led_mu;
    std::atomic<bool> running{true};
};

inline Globals& globals() {
    static Globals g;
    return g;
}

inline void set_led_mode(LedMode mode) {
    globals().led_mode.store(static_cast<int>(mode));
}

} // namespace ft
