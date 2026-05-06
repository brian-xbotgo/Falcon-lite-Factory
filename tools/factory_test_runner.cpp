// factory_test_runner.cpp
// Factory test runner -- sends full set of factory test commands to device prodTest daemon via MQTT
//
// Usage:
//   ./factory_test_runner [OPTIONS]
//
// Follows the binary protocol defined in factory_proto.md:
//   Request:  38 bytes (SN=16 + bizid=16 + msgid=8)
//   Response: 42 bytes (38 header + 4 bytes error_code big-endian)
//
// Options:
//   --stage pcba    Board-level test (10R~18R, 191R, 192R)
//   --stage system  System-level test (20R~28R, 291R~296R)
//   --stage aging   Aging test (30R/32R/34R/36R)
//   --stage all     All tests (default)
//   --test 12R      Run a single test
//   --broker,-b     MQTT broker address (default 127.0.0.1)
//   --port,-p       MQTT port (default 1883)
//   --sn,-s         Device serial number (default TEST000000000000)
//   --list          List all supported test items
//   --dry-run       Print requests only, do not send

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <string>
#include <vector>
#include <map>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <functional>
#include <algorithm>
#include <sstream>
#include <iomanip>

#include <mosquitto.h>
#include <getopt.h>

// ============================================================
// Protocol constants (consistent with common/proto.hpp and factory_proto.md)
// ============================================================
static constexpr size_t PROTO_SN_LEN     = 16;
static constexpr size_t PROTO_BIZID_LEN  = 16;
static constexpr size_t PROTO_MSGID_LEN  = 8;
static constexpr size_t PROTO_HEADER_LEN = PROTO_SN_LEN + PROTO_BIZID_LEN + PROTO_MSGID_LEN; // 40
static constexpr size_t PROTO_RESP_LEN   = PROTO_HEADER_LEN + 4;  // 44

static constexpr int MQTT_QOS = 2;

// ============================================================
// Big-endian conversion
// ============================================================
static uint32_t from_big_endian(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8)  |
           (static_cast<uint32_t>(p[3]));
}

// ============================================================
// Error bit descriptors
// ============================================================
struct ErrorBit {
    int bit;
    uint32_t mask;
    const char* name;
};

using ErrorBits = std::vector<ErrorBit>;

// ============================================================
// Test case definition
// ============================================================
struct TestCase {
    std::string topic_req;       // Request topic (e.g. "12R")
    std::string topic_resp;      // Response topic (e.g. "12A")
    std::string name;            // Display name
    std::string stage;           // Stage: pcba / system / aging / aux
    int timeout_sec;             // Timeout in seconds
    bool has_response;           // Whether a response is expected
    bool is_raw_payload;         // true=non-standard payload (e.g. 37R WiFi connection)
    ErrorBits error_bits;        // Error bit definitions
    std::string description;     // Description
};

// ============================================================
// Build all test cases
// ============================================================
static std::vector<TestCase> build_all_tests() {
    std::vector<TestCase> tests;

    // ==================== PCBA board-level tests ====================

    tests.push_back({
        "10R", "10A", "RTC+IC Detection", "pcba", 30, true, false,
        {
            { 0, 0x00000001, "Failed to open RTC device" },
            { 1, 0x00000002, "Failed to read RTC time" },
            { 2, 0x00000004, "RTC year earlier than 2026" },
            { 3, 0x00000008, "Failed to read system time" },
            { 4, 0x00000010, "Failed to convert local time" },
            { 5, 0x00000020, "RTC and system time mismatch" },
            { 6, 0x00000040, "I2C query command failed" },
            { 7, 0x00000080, "Charger IC detection failed" },
            { 8, 0x00000100, "Charger IC read value failed" },
            { 9, 0x00000200, "Fuel gauge IC detection failed" },
            {10, 0x00000400, "Fuel gauge IC read value failed" },
            {11, 0x00000800, "Fuel gauge device not readable" },
        },
        "RTC clock + Charger IC(0x75) + Fuel Gauge IC(0x38)"
    });

    tests.push_back({
        "11R", "11A", "Microphone", "pcba", 15, true, false,
        {
            { 0, 0x00000001, "Failed to set microphone gain" },
            { 1, 0x00000002, "Recording command failed" },
        },
        "Buzzer+Mic recording 5 seconds"
    });

    tests.push_back({
        "12R", "12A", "SoC", "pcba", 120, true, false,
        {
            { 0, 0x00000001, "GPU test failed" },
            { 1, 0x00000002, "DDR test failed" },
            { 2, 0x00000004, "CPU test failed" },
            { 3, 0x00000008, "NPU test failed" },
            { 4, 0x00000010, "eMMC test failed" },
        },
        "GPU/DDR/CPU/NPU/eMMC five-subsystem stress test"
    });

    tests.push_back({
        "13R", "", "Bluetooth", "pcba", 5, false, false, {},
        "Log only, no actual test logic"
    });

    tests.push_back({
        "14R", "14A", "TF Card", "pcba", 20, true, false,
        {
            { 0, 0x00000001, "TF card write speed below 10MB/s" },
        },
        "SD card read/write speed test (requires internal SD service)"
    });

    tests.push_back({
        "15R", "15A", "Battery (Board-level)", "pcba", 10, true, false,
        {
            { 0, 0x00000001, "Voltage below 3.6V" },
            { 1, 0x00000002, "Failed to read battery info" },
        },
        "Battery voltage check >= 3.6V"
    });

    tests.push_back({
        "16R", "16A", "WiFi Scan", "pcba", 20, true, false,
        {
            { 0, 0x00000001, "WiFi scan failed" },
        },
        "wpa_cli scan check"
    });

    tests.push_back({
        "17R", "17A", "Buttons (Board-level)", "pcba", 20, true, false,
        {
            { 0, 0x00000001, "KEY_POWER not detected" },
            { 1, 0x00000002, "KEY_F13 not detected" },
            { 2, 0x00000004, "KEY_F14 not detected" },
            { 3, 0x00000008, "KEY_F15 not detected" },
            { 4, 0x00000010, "KEY_F16 not detected" },
            { 5, 0x00000020, "KEY_F17 not detected" },
        },
        "6-button detection (any 2 passes)"
    });

    tests.push_back({
        "18R", "18A", "2K Camera", "pcba", 30, true, false,
        {
            { 0, 0x00000001, "Camera interface error (MIPI / video node)" },
            { 1, 0x00000002, "Sensor I2C probe failed" },
            { 2, 0x00000004, "Camera not detected" },
            { 3, 0x00000008, "OTP calibration check failed" },
        },
        "gc4663 2K camera HW test (OTP+I2C+MIPI)"
    });

    // Board-level motors (no response)
    tests.push_back({
        "191R", "", "Vertical Motor (Board-level)", "pcba", 10, false, false, {},
        "TMI8152 CH34 forward 1024 cycles (manual observation)"
    });

    tests.push_back({
        "192R", "", "Horizontal Motor (Board-level)", "pcba", 10, false, false, {},
        "TMI8152 CH12 forward 1024 cycles (manual observation)"
    });

    // ==================== System-level tests ====================

    tests.push_back({
        "20R", "20A", "Buttons (System-level)", "system", 30, true, false,
        {
            { 0, 0x00000001, "KEY_POWER not detected" },
            { 1, 0x00000002, "KEY_F13 not detected" },
            { 2, 0x00000004, "KEY_F14 not detected" },
            { 3, 0x00000008, "KEY_F15 not detected" },
            { 4, 0x00000010, "KEY_F16 not detected" },
            { 5, 0x00000020, "KEY_F17 not detected" },
        },
        "6-button full detection (real-time per key)"
    });

    tests.push_back({
        "21R", "21A", "Vertical Hall", "system", 30, true, false,
        {
            { 0, 0x00000001, "Upper limit hall not detected" },
            { 1, 0x00000002, "Lower limit hall not detected" },
        },
        "Tilt motor upper/lower limit hall switch detection"
    });

    tests.push_back({
        "22R", "22A", "4K Camera", "system", 30, true, false,
        {
            { 0, 0x00000001, "Camera interface error (MIPI / video node)" },
            { 1, 0x00000002, "Sensor I2C probe failed" },
            { 2, 0x00000004, "Camera not detected" },
            { 3, 0x00000008, "OTP calibration check failed" },
        },
        "imx678 4K camera HW test (OTP+I2C+MIPI)"
    });

    tests.push_back({
        "23R", "23A", "Microphone (System-level)", "system", 15, true, false,
        {
            { 0, 0x00000001, "Failed to set microphone gain" },
            { 1, 0x00000002, "Recording command failed" },
        },
        "System-level microphone + buzzer test"
    });

    tests.push_back({
        "24R", "24A", "Battery (System-level)", "system", 10, true, false,
        {
            { 0, 0x00000001, "Voltage below 3.6V" },
            { 1, 0x00000002, "Failed to read battery info" },
        },
        "System-level battery voltage check"
    });

    tests.push_back({
        "25R", "", "WiFi (System-level)", "system", 5, false, false, {},
        "Log only, no actual test logic"
    });

    tests.push_back({
        "26R", "26A", "Latch Detection", "system", 30, true, false,
        {
            { 0, 0x00000001, "Latch not toggled twice within 20s" },
        },
        "GPIO98 latch switch toggle detection"
    });

    tests.push_back({
        "27R", "27A", "Horizontal Hall", "system", 30, true, false,
        {
            { 0, 0x00000001, "Hall position manager not initialized" },
            { 2, 0x00000004, "Max voltage below 2.0V" },
            { 3, 0x00000008, "Min voltage below 1.6V" },
        },
        "Horizontal motor 360deg + hall voltage sampling"
    });

    tests.push_back({
        "28R", "28A", "2K Camera (System-level)", "system", 30, true, false,
        {
            { 0, 0x00000001, "Camera interface error (MIPI / video node)" },
            { 1, 0x00000002, "Sensor I2C probe failed" },
            { 2, 0x00000004, "Camera not detected" },
            { 3, 0x00000008, "OTP calibration check failed" },
        },
        "gc4663 2K camera HW retest"
    });

    // System-level motors
    tests.push_back({
        "291R", "", "Vertical Motor (System-level)", "system", 10, false, false, {},
        "Continuous up/down cycling (stop via 293R)"
    });

    tests.push_back({
        "292R", "", "Horizontal Motor (System-level)", "system", 10, false, false, {},
        "+/-90deg cycling (stop via 294R)"
    });

    tests.push_back({
        "293R", "", "Stop Vertical Motor", "system", 5, false, false, {},
        "Stop 291R vertical motor"
    });

    tests.push_back({
        "294R", "", "Stop Horizontal Motor", "system", 5, false, false, {},
        "Stop 292R horizontal motor"
    });

    tests.push_back({
        "295R", "295A", "Vertical Motor + Recording", "system", 20, true, false,
        {},
        "Vertical motor + 10s recording -> v_motor.wav (host analyzes audio)"
    });

    tests.push_back({
        "296R", "296A", "Horizontal Motor + Recording", "system", 20, true, false,
        {},
        "Horizontal motor + 10s recording -> h_motor.wav (host analyzes audio)"
    });

    // ==================== Aging / Auxiliary ====================

    tests.push_back({
        "30R", "", "Full Aging", "aging", 10, false, false, {},
        "Start recording+motor+WiFi/CAM detection (duration controlled by aging_time.conf)"
    });

    tests.push_back({
        "31R", "31A", "Heartbeat", "aux", 5, true, true, {},
        "Echo reply, check device online"
    });

    tests.push_back({
        "32R", "", "Motor Aging", "aging", 10, false, false, {},
        "Motor-only cycling aging (no WiFi/CAM/recording)"
    });

    tests.push_back({
        "34R", "", "Aging Stop", "aging", 10, false, false, {},
        "Stop aging, LED returns to red/white alternating"
    });

    tests.push_back({
        "36R", "", "Aging Failed", "aging", 10, false, false, {},
        "Mark aging failed, LED red flash"
    });

    tests.push_back({
        "37R", "37A", "WiFi Connect", "aux", 70, true, true, {},
        "Connect to specified WiFi: payload=SSID--wifi--PASSWORD, response=IP address"
    });

    tests.push_back({
        "38R", "38A", "Charger Status", "aux", 10, true, false,
        {},
        "Read charger status (requires CHANGER topic first)"
    });

    tests.push_back({
        "50R", "", "Shutdown", "aux", 5, false, false, {},
        "Execute power_off.sh shutdown (use with caution!)"
    });

    return tests;
}

// ============================================================
// Test result
// ============================================================
struct TestResult {
    std::string topic_req;
    std::string name;
    bool executed;
    bool passed;
    int exit_code;         // 0=pass, 1=fail, 2=timeout, 3=error, 4=skipped
    std::string detail;
    uint32_t error_code;
    double elapsed_ms;
};

// ============================================================
// Test runner
// ============================================================
class FactoryTestRunner {
public:
    struct Config {
        std::string broker    = "127.0.0.1";
        int         port      = 1883;
        std::string sn        = "TEST000000000000";
        std::string bizid     = "0000000000000000";
        std::string msgid     = "00000000";
        std::string stage     = "all";       // pcba / system / aging / all
        std::string test_filter;             // Run a single test topic
        bool        dry_run   = false;
        bool        list_only = false;
        int         global_timeout = 300;
    };

    FactoryTestRunner(const Config& cfg) : cfg_(cfg) {
        tests_ = build_all_tests();
    }

    bool init() {
        mosquitto_lib_init();
        mosq_ = mosquitto_new("factory_runner", true, this);
        if (!mosq_) {
            std::fprintf(stderr, "[runner] mosquitto_new failed\n");
            return false;
        }
        mosquitto_connect_callback_set(mosq_, on_connect_cb);
        mosquitto_disconnect_callback_set(mosq_, on_disconnect_cb);
        mosquitto_message_callback_set(mosq_, on_message_cb);
        return true;
    }

    bool connect() {
        int rc = mosquitto_connect(mosq_, cfg_.broker.c_str(), cfg_.port, 60);
        if (rc != MOSQ_ERR_SUCCESS) {
            std::fprintf(stderr, "[runner] connect to %s:%d failed: %s\n",
                         cfg_.broker.c_str(), cfg_.port, mosquitto_strerror(rc));
            return false;
        }
        rc = mosquitto_loop_start(mosq_);
        if (rc != MOSQ_ERR_SUCCESS) {
            std::fprintf(stderr, "[runner] loop_start failed: %s\n", mosquitto_strerror(rc));
            return false;
        }
        // Wait for connection to establish
        int wait_ms = 0;
        while (!connected_ && wait_ms < 5000) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            wait_ms += 100;
        }
        if (!connected_) {
            std::fprintf(stderr, "[runner] connect timeout\n");
            return false;
        }
        return true;
    }

    void disconnect() {
        done_ = true;
        if (mosq_) {
            mosquitto_disconnect(mosq_);
            mosquitto_loop_stop(mosq_, false);
            mosquitto_destroy(mosq_);
            mosq_ = nullptr;
        }
        mosquitto_lib_cleanup();
    }

    // Run all or filtered tests
    std::vector<TestResult> run_all() {
        std::vector<TestResult> results;

        for (auto& tc : tests_) {
            // Stage filter
            if (cfg_.stage != "all" && tc.stage != cfg_.stage) {
                // aux always includes heartbeat (31R), other aux need explicit --test
                if (tc.topic_req != "31R") continue;
            }
            // 50R shutdown requires explicit --test 50R
            if (cfg_.test_filter.empty() && tc.topic_req == "50R") {
                continue;
            }
            // 单独测试过滤
            if (!cfg_.test_filter.empty() && tc.topic_req != cfg_.test_filter) {
                continue;
            }

            TestResult r;
            r.topic_req = tc.topic_req;
            r.name      = tc.name;
            r.executed  = true;
            r.error_code = 0;
            r.elapsed_ms = 0;

            if (cfg_.dry_run) {
                r.exit_code = 4;  // skipped
                r.detail    = "(dry-run)";
                results.push_back(r);
                continue;
            }

            std::fprintf(stderr, "\n----------------------------------------\n");
            std::fprintf(stderr, "[RUN ] %s (%s) -- %s\n", tc.name.c_str(), tc.topic_req.c_str(), tc.description.c_str());
            std::fprintf(stderr, "----------------------------------------\n");

            if (tc.has_response) {
                r = execute_with_response(tc);
            } else {
                r = execute_fire_and_forget(tc);
            }

            std::fprintf(stderr, "[%s] %s (%.0fms)\n",
                         exit_code_label(r.exit_code), r.detail.c_str(), r.elapsed_ms);
            results.push_back(r);
        }

        return results;
    }

    void print_summary(const std::vector<TestResult>& results) {
        int total = 0, passed = 0, failed = 0, timeout = 0, skipped = 0;

        std::fprintf(stderr, "\n\n");
        std::fprintf(stderr, "+==================================================================+\n");
        std::fprintf(stderr, "|                Factory Test Results Summary                 |\n");
        std::fprintf(stderr, "+==================================================================+\n");
        std::fprintf(stderr, "| %-4s | %-16s | %-7s | %-6s | %s\n", "#", "Test Item", "Result", "Time(ms)", "Details");
        std::fprintf(stderr, "+==================================================================+\n");

        for (size_t i = 0; i < results.size(); ++i) {
            const auto& r = results[i];
            if (!r.executed) continue;
            ++total;

            const char* label;
            switch (r.exit_code) {
                case 0: ++passed;  label = "PASS";   break;
                case 1: ++failed;  label = "FAIL";   break;
                case 2: ++timeout; label = "TIMEOUT"; break;
                case 3: ++failed;  label = "ERROR";  break;
                default: ++skipped; label = "SKIP";  break;
            }

            std::fprintf(stderr, "| %-4zu | %-16s | %-7s | %-6.0f | %s\n",
                         i + 1, r.name.c_str(), label, r.elapsed_ms, r.detail.c_str());

            // Print error bits on failure
            if (r.exit_code == 1 && r.error_code != 0) {
                std::fprintf(stderr, "|      | error_code=0x%08X  |         |        |\n", r.error_code);
            }
        }

        std::fprintf(stderr, "+==================================================================+\n");
        std::fprintf(stderr, "| Total: %d  | Passed: %d  | Failed: %d  | Timeout: %d  | Skipped: %d\n",
                     total, passed, failed, timeout, skipped);
        std::fprintf(stderr, "+==================================================================+\n");

        if (failed > 0 || timeout > 0) {
            std::fprintf(stderr, "\n[runner] Failures present, exit code = 1\n");
        } else {
            std::fprintf(stderr, "\n[runner] All passed!\n");
        }
    }

private:
    Config cfg_;
    std::vector<TestCase> tests_;
    struct mosquitto* mosq_ = nullptr;
    std::atomic<bool> connected_{false};
    std::atomic<bool> done_{false};

    // Response wait state
    std::mutex resp_mutex_;
    std::condition_variable resp_cv_;
    std::string resp_topic_;
    std::string resp_payload_;
    bool resp_received_ = false;

    static const char* exit_code_label(int code) {
        switch (code) {
            case 0: return "PASS";
            case 1: return "FAIL";
            case 2: return "TIME";
            case 3: return "ERR ";
            default: return "SKIP";
        }
    }

    void reset_response_state(const std::string& expect_topic) {
        std::lock_guard<std::mutex> lk(resp_mutex_);
        resp_topic_ = expect_topic;
        resp_payload_.clear();
        resp_received_ = false;
    }

    // ---- MQTT callbacks ----
    static void on_connect_cb(struct mosquitto*, void* ud, int rc) {
        auto* self = static_cast<FactoryTestRunner*>(ud);
        if (rc == 0) {
            self->connected_ = true;
            std::fprintf(stderr, "[runner] connected to broker\n");
        } else {
            std::fprintf(stderr, "[runner] connect failed: %s\n", mosquitto_strerror(rc));
        }
    }

    static void on_disconnect_cb(struct mosquitto*, void* ud, int rc) {
        auto* self = static_cast<FactoryTestRunner*>(ud);
        self->connected_ = false;
        if (rc != 0 && !self->done_) {
            std::fprintf(stderr, "[runner] unexpected disconnect: %s\n", mosquitto_strerror(rc));
        }
    }

    static void on_message_cb(struct mosquitto*, void* ud, const struct mosquitto_message* msg) {
        auto* self = static_cast<FactoryTestRunner*>(ud);
        if (!msg || !msg->topic || !msg->payload) return;

        std::lock_guard<std::mutex> lk(self->resp_mutex_);
        // Only handle response for the currently expected topic
        if (self->resp_topic_.empty()) return;
        if (std::strcmp(msg->topic, self->resp_topic_.c_str()) != 0) return;

        self->resp_payload_.assign(static_cast<const char*>(msg->payload),
                                   static_cast<size_t>(msg->payloadlen));
        self->resp_received_ = true;
        self->resp_cv_.notify_one();
    }

    // ---- Send 38-byte standard request ----
    std::string build_request() {
        std::string req(PROTO_HEADER_LEN, '\0');
        std::memcpy(&req[0], cfg_.sn.c_str(), std::min(cfg_.sn.size(), PROTO_SN_LEN));
        std::memcpy(&req[PROTO_SN_LEN], cfg_.bizid.c_str(), std::min(cfg_.bizid.size(), PROTO_BIZID_LEN));
        std::memcpy(&req[PROTO_SN_LEN + PROTO_BIZID_LEN], cfg_.msgid.c_str(), std::min(cfg_.msgid.size(), PROTO_MSGID_LEN));
        return req;
    }

    bool publish_and_subscribe(const std::string& topic_req, const std::string& payload) {
        // Subscribe to response topic first
        std::string resp_topic = topic_req;
        if (!resp_topic.empty() && resp_topic.back() == 'R') {
            resp_topic.back() = 'A';
        }
        if (!resp_topic.empty() && resp_topic != topic_req) {
            mosquitto_subscribe(mosq_, nullptr, resp_topic.c_str(), MQTT_QOS);
        }

        int rc = mosquitto_publish(mosq_, nullptr, topic_req.c_str(),
                                   static_cast<int>(payload.size()),
                                   payload.data(), MQTT_QOS, false);
        if (rc != MOSQ_ERR_SUCCESS) {
            std::fprintf(stderr, "[runner] publish %s failed: %s\n", topic_req.c_str(), mosquitto_strerror(rc));
            return false;
        }
        std::fprintf(stderr, "[runner] -> %s (%zu bytes)\n", topic_req.c_str(), payload.size());
        return true;
    }

    // ---- Standard protocol: send request, wait for 42-byte response ----
    TestResult execute_with_response(const TestCase& tc) {
        TestResult r;
        r.topic_req = tc.topic_req;
        r.name = tc.name;
        r.executed = true;

        auto t0 = std::chrono::steady_clock::now();

        // Prepare expected response topic
        std::string expect_topic = tc.topic_resp;
        reset_response_state(expect_topic);

        // Build request
        std::string request;
        if (tc.is_raw_payload) {
            // 37R WiFi connect needs special payload, default empty
            // 31R Heartbeat uses any payload
            request = build_request();
        } else {
            request = build_request();
        }

        // Send
        if (!publish_and_subscribe(tc.topic_req, request)) {
            auto t1 = std::chrono::steady_clock::now();
            r.elapsed_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
            r.exit_code = 3;
            r.detail = "publish failed";
            return r;
        }

        // Wait for response
        {
            std::unique_lock<std::mutex> lk(resp_mutex_);
            bool got = resp_cv_.wait_for(lk, std::chrono::seconds(tc.timeout_sec),
                                         [this] { return resp_received_; });
            auto t1 = std::chrono::steady_clock::now();
            r.elapsed_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

            if (!got) {
                r.exit_code = 2;
                r.detail = "timeout -- no response";
                return r;
            }
        }

        // Parse response
        auto t1 = std::chrono::steady_clock::now();
        r.elapsed_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

        if (tc.is_raw_payload && tc.topic_req == "37R") {
            // WiFi connect response is ASCII IP address
            r.exit_code = 0;
            r.error_code = 0;
            r.detail = "IP=" + resp_payload_;
            return r;
        }

        if (tc.is_raw_payload && tc.topic_req == "31R") {
            // Heartbeat echo
            r.exit_code = 0;
            r.error_code = 0;
            r.detail = "echo OK (" + std::to_string(resp_payload_.size()) + " bytes)";
            return r;
        }

        // Standard 42-byte response (or 43-byte for 38R)
        if (resp_payload_.size() < PROTO_HEADER_LEN + 4) {
            r.exit_code = 3;
            r.detail = "response too short: " + std::to_string(resp_payload_.size()) + " bytes";
            return r;
        }

        const auto* data = reinterpret_cast<const uint8_t*>(resp_payload_.data());
        r.error_code = from_big_endian(data + PROTO_HEADER_LEN);

        if (r.error_code == 0) {
            r.exit_code = 0;
            r.detail = "OK";
        } else {
            r.exit_code = 1;
            // Parse error bits
            std::vector<std::string> failures;
            for (const auto& eb : tc.error_bits) {
                if (r.error_code & eb.mask) {
                    failures.push_back(eb.name);
                }
            }
            if (failures.empty()) {
                r.detail = "error_code=0x" + to_hex(r.error_code);
            } else {
                r.detail = "";
                for (size_t i = 0; i < failures.size(); ++i) {
                    if (i > 0) r.detail += ", ";
                    r.detail += failures[i];
                }
            }
            std::fprintf(stderr, "[runner] error bits: %s\n", r.detail.c_str());
        }

        // 38R has extra 1 byte charger_status
        if (tc.topic_req == "38R" && resp_payload_.size() >= PROTO_HEADER_LEN + 5) {
            uint8_t chg = static_cast<uint8_t>(resp_payload_[PROTO_HEADER_LEN + 4]);
            r.detail += " | charger_status=" + std::to_string(chg);
        }

        return r;
    }

    // ---- Fire and forget (no response) ----
    TestResult execute_fire_and_forget(const TestCase& tc) {
        TestResult r;
        r.topic_req = tc.topic_req;
        r.name = tc.name;
        r.executed = true;

        auto t0 = std::chrono::steady_clock::now();

        std::string request = build_request();
        bool ok = publish_and_subscribe(tc.topic_req, request);

        auto t1 = std::chrono::steady_clock::now();
        r.elapsed_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

        if (!ok) {
            r.exit_code = 3;
            r.detail = "publish failed";
        } else {
            r.exit_code = 0;
            r.detail = "sent (no response expected)";
        }

        return r;
    }

    static std::string to_hex(uint32_t val) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%08X", val);
        return buf;
    }

public:
    void list_tests() {
        std::fprintf(stderr, "%-6s | %-20s | %-6s | %-4s | %-5s | %s\n",
                     "Topic", "Name", "Stage", "Resp", "Async", "Description");
        std::fprintf(stderr, "-------+----------------------+--------+------+-------+----------\n");
        for (const auto& tc : tests_) {
            std::fprintf(stderr, "%-6s | %-20s | %-6s | %-4s | %-5s | %s\n",
                         tc.topic_req.c_str(),
                         tc.name.c_str(),
                         tc.stage.c_str(),
                         tc.has_response ? "yes" : "no",
                         tc.is_raw_payload ? "raw" : "std",
                         tc.description.c_str());
        }
    }
};

// ============================================================
// Usage
// ============================================================
static void print_usage(const char* prog) {
    std::fprintf(stderr,
        "Usage: %s [OPTIONS]\n"
        "\n"
        "Factory test runner -- sends full set of factory test commands to device prodTest daemon via MQTT.\n"
        "Follows the binary protocol defined in factory_proto.md.\n"
        "\n"
        "Options:\n"
        "  --stage, -S   Test stage: pcba | system | aging | all (default: all)\n"
        "  --test,  -T   Run a single test topic (e.g. --test 12R)\n"
        "  --broker, -b  MQTT broker address   (default: 127.0.0.1)\n"
        "  --port,   -p  MQTT broker port      (default: 1883)\n"
        "  --sn,     -s  Device serial number  (default: TEST000000000000)\n"
        "  --bizid,  -B  Business ID           (default: 0000000000000000)\n"
        "  --msgid,  -M  Message ID            (default: 00000000)\n"
        "  --timeout,-t  Global timeout seconds(default: 300)\n"
        "  --list,   -l  List all test items\n"
        "  --dry-run,-n  Print only, do not send\n"
        "  --help,   -h  Show help\n"
        "\n"
        "Test stages:\n"
        "  pcba   -- Board-level tests (10R~18R, 191R, 192R)\n"
        "  system -- System-level tests (20R~28R, 291R~296R)\n"
        "  aging  -- Aging tests (30R, 32R, 34R, 36R)\n"
        "  all    -- All tests\n"
        "\n"
        "Examples:\n"
        "  %s --stage pcba --sn ABC1234567890123\n"
        "  %s --test 12R --broker 192.168.1.100\n"
        "  %s --stage all --dry-run\n"
        "  %s --list\n",
        prog, prog, prog, prog, prog);
}

// ============================================================
// main
// ============================================================
int main(int argc, char** argv) {
    FactoryTestRunner::Config cfg;

    static const struct option long_opts[] = {
        {"stage",   required_argument, nullptr, 'S'},
        {"test",    required_argument, nullptr, 'T'},
        {"broker",  required_argument, nullptr, 'b'},
        {"port",    required_argument, nullptr, 'p'},
        {"sn",      required_argument, nullptr, 's'},
        {"bizid",   required_argument, nullptr, 'B'},
        {"msgid",   required_argument, nullptr, 'M'},
        {"timeout", required_argument, nullptr, 't'},
        {"list",    no_argument,       nullptr, 'l'},
        {"dry-run", no_argument,       nullptr, 'n'},
        {"help",    no_argument,       nullptr, 'h'},
        {nullptr,   0,                 nullptr, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "S:T:b:p:s:B:M:t:lnh", long_opts, nullptr)) != -1) {
        switch (opt) {
            case 'S': cfg.stage       = optarg; break;
            case 'T': cfg.test_filter = optarg; break;
            case 'b': cfg.broker      = optarg; break;
            case 'p': cfg.port        = std::atoi(optarg); break;
            case 's': cfg.sn          = optarg; break;
            case 'B': cfg.bizid       = optarg; break;
            case 'M': cfg.msgid       = optarg; break;
            case 't': cfg.global_timeout = std::atoi(optarg); break;
            case 'l': cfg.list_only   = true; break;
            case 'n': cfg.dry_run     = true; break;
            case 'h': print_usage(argv[0]); return 0;
            default:  print_usage(argv[0]); return 1;
        }
    }

    FactoryTestRunner runner(cfg);

    if (!runner.init()) {
        std::fprintf(stderr, "[runner] init failed\n");
        return 1;
    }

    // --list mode
    if (cfg.list_only) {
        runner.list_tests();
        runner.disconnect();
        return 0;
    }

    // Print run info
    std::fprintf(stderr, "+==============================================================+\n");
    std::fprintf(stderr, "|       Falcon_Air_F Factory Test Runner               |\n");
    std::fprintf(stderr, "+==============================================================+\n");
    std::fprintf(stderr, "| Broker:  %s:%d\n", cfg.broker.c_str(), cfg.port);
    std::fprintf(stderr, "| SN:      %s\n", cfg.sn.c_str());
    std::fprintf(stderr, "| Stage:   %s\n", cfg.stage.c_str());
    if (!cfg.test_filter.empty()) {
        std::fprintf(stderr, "| Test:    %s\n", cfg.test_filter.c_str());
    }
    if (cfg.dry_run) {
        std::fprintf(stderr, "| Mode:    DRY-RUN (not sending)\n");
    }
    std::fprintf(stderr, "+==============================================================+\n\n");

    if (!cfg.dry_run) {
        if (!runner.connect()) {
            std::fprintf(stderr, "[runner] connect failed\n");
            runner.disconnect();
            return 1;
        }
    }

    auto results = runner.run_all();
    runner.print_summary(results);

    if (!cfg.dry_run) {
        runner.disconnect();
    }

    // Compute exit code
    for (const auto& r : results) {
        if (r.exit_code == 1 || r.exit_code == 2 || r.exit_code == 3) {
            return 1;
        }
    }
    return 0;
}
