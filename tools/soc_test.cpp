// soc_test — 独立 SoC 测试 MQTT 客户端
//
// 用法:
//   ./soc_test [SN] [bizid] [msgid] [--broker 127.0.0.1] [--port 1883] [--timeout 120]
//
// 连接到 MQTT broker，发送 12R 请求，等待 12A 响应，解析并打印各项测试结果。
// 遵循 factory_proto 二进制协议:
//   请求: 38 字节 (SN=14 + bizid=16 + msgid=8)
//   响应: 42 字节 (38 头 + 4 字节 error_code 大端序)

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <chrono>

#include <mosquitto.h>
#include <getopt.h>

// ============================================================
// 协议常量 (与 common/proto.hpp 和 factory_proto.md 一致)
// ============================================================
static constexpr size_t PROTO_SN_LEN     = 14;
static constexpr size_t PROTO_BIZID_LEN  = 16;
static constexpr size_t PROTO_MSGID_LEN  = 8;
static constexpr size_t PROTO_HEADER_LEN = PROTO_SN_LEN + PROTO_BIZID_LEN + PROTO_MSGID_LEN; // 38
static constexpr size_t PROTO_RESP_LEN   = PROTO_HEADER_LEN + 4;  // 42

// error_code 位定义 (与 soc.hpp 一致)
static constexpr uint32_t ERR_GPU  = 0x0001;  // bit 0
static constexpr uint32_t ERR_DDR  = 0x0002;  // bit 1
static constexpr uint32_t ERR_CPU  = 0x0004;  // bit 2
static constexpr uint32_t ERR_NPU  = 0x0008;  // bit 3
static constexpr uint32_t ERR_EMMC = 0x0010;  // bit 4

static constexpr const char* REQ_TOPIC  = "12R";
static constexpr const char* RESP_TOPIC = "12A";
static constexpr int         MQTT_QOS   = 2;

// ============================================================
// 应用程序状态
// ============================================================
struct AppState {
    // 用户参数
    std::string sn      = "TEST000000000000";
    std::string bizid   = "0000000000000000";
    std::string msgid   = "00000000";
    std::string broker  = "127.0.0.1";
    int         port    = 1883;
    int         timeout_sec = 120;

    // MQTT 状态
    struct mosquitto* mosq = nullptr;
    std::atomic<bool> connected{false};
    std::atomic<bool> done{false};

    // 响应数据
    std::mutex              resp_mutex;
    std::condition_variable resp_cv;
    bool                    resp_received = false;
    std::string             resp_payload;

    // 最终结果
    int exit_code = 1;  // 默认失败
};

// ============================================================
// 大端序转换 (与 proto.hpp 一致)
// ============================================================
static uint32_t from_big_endian(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8)  |
           (static_cast<uint32_t>(p[3]));
}

// ============================================================
// MQTT 回调
// ============================================================
static void on_connect_cb(struct mosquitto*, void* ud, int rc) {
    auto* st = static_cast<AppState*>(ud);

    if (rc != 0) {
        std::fprintf(stderr, "[soc_test] connect failed: %s (rc=%d)\n",
                     mosquitto_strerror(rc), rc);
        return;
    }

    st->connected = true;
    std::fprintf(stderr, "[soc_test] connected to %s:%d\n",
                 st->broker.c_str(), st->port);

    // 订阅 12A
    int sub_rc = mosquitto_subscribe(st->mosq, nullptr, RESP_TOPIC, MQTT_QOS);
    if (sub_rc != MOSQ_ERR_SUCCESS) {
        std::fprintf(stderr, "[soc_test] subscribe %s failed: %s (rc=%d)\n",
                     RESP_TOPIC, mosquitto_strerror(sub_rc), sub_rc);
        return;
    }
    std::fprintf(stderr, "[soc_test] subscribed to %s\n", RESP_TOPIC);

    // 构造 38 字节请求 payload
    char payload[PROTO_HEADER_LEN] = {};
    std::memcpy(payload, st->sn.c_str(),    std::min(st->sn.size(),    PROTO_SN_LEN));
    std::memcpy(payload + PROTO_SN_LEN,     st->bizid.c_str(), std::min(st->bizid.size(), PROTO_BIZID_LEN));
    std::memcpy(payload + PROTO_SN_LEN + PROTO_BIZID_LEN, st->msgid.c_str(), std::min(st->msgid.size(), PROTO_MSGID_LEN));

    int pub_rc = mosquitto_publish(st->mosq, nullptr, REQ_TOPIC,
                                   static_cast<int>(sizeof(payload)),
                                   payload, MQTT_QOS, false);
    if (pub_rc != MOSQ_ERR_SUCCESS) {
        std::fprintf(stderr, "[soc_test] publish 12R failed: %s (rc=%d)\n",
                     mosquitto_strerror(pub_rc), pub_rc);
        return;
    }
    std::fprintf(stderr, "[soc_test] 12R request sent, SN=%.16s\n", st->sn.c_str());
    std::fprintf(stderr, "[soc_test] waiting for 12A response (timeout=%ds)...\n",
                 st->timeout_sec);
}

static void on_disconnect_cb(struct mosquitto*, void* ud, int rc) {
    auto* st = static_cast<AppState*>(ud);
    st->connected = false;

    if (rc != 0 && !st->done) {
        std::fprintf(stderr, "[soc_test] unexpected disconnect: %s (rc=%d)\n",
                     mosquitto_strerror(rc), rc);
        std::lock_guard<std::mutex> lk(st->resp_mutex);
        st->resp_received = true;  // 唤醒等待线程
        st->resp_cv.notify_one();
    }
}

static void on_message_cb(struct mosquitto*, void* ud,
                          const struct mosquitto_message* msg) {
    auto* st = static_cast<AppState*>(ud);

    if (!msg || !msg->topic || !msg->payload) return;
    if (std::strcmp(msg->topic, RESP_TOPIC) != 0) return;

    std::fprintf(stderr, "[soc_test] 12A received, %d bytes\n", msg->payloadlen);

    // 校验最小长度
    if (static_cast<size_t>(msg->payloadlen) < PROTO_HEADER_LEN + 4) {
        std::fprintf(stderr, "[soc_test] ERROR: response too short (%d bytes, expected >= %zu)\n",
                     msg->payloadlen, PROTO_HEADER_LEN + 4);
        return;
    }

    // 存储响应并通知主线程
    {
        std::lock_guard<std::mutex> lk(st->resp_mutex);
        st->resp_payload.assign(static_cast<const char*>(msg->payload),
                                static_cast<size_t>(msg->payloadlen));
        st->resp_received = true;
    }
    st->resp_cv.notify_one();
}

// ============================================================
// 打印结果
// ============================================================
static void print_result(const char* name, int bit, uint32_t error_code) {
    bool pass = (error_code & (1u << bit)) == 0;
    std::fprintf(stderr, "  %-5s (bit%d): %s\n", name, bit, pass ? "PASS" : "FAIL");
}

// ============================================================
// 解析并打印错误码
// ============================================================
static int parse_and_print_error(const std::string& payload) {
    // 从 offset 38 开始读取 4 字节 error_code (大端序)
    const auto* data = reinterpret_cast<const uint8_t*>(payload.data());
    uint32_t error_code = from_big_endian(data + PROTO_HEADER_LEN);

    std::fprintf(stderr, "[soc_test] error_code = 0x%08X\n", error_code);

    print_result("GPU",  0, error_code);
    print_result("DDR",  1, error_code);
    print_result("CPU",  2, error_code);
    print_result("NPU",  3, error_code);
    print_result("eMMC", 4, error_code);

    if (error_code == 0) {
        std::fprintf(stderr, "[soc_test] ALL TESTS PASSED\n");
        return 0;
    } else {
        std::fprintf(stderr, "[soc_test] SOME TESTS FAILED (error_code=0x%08X)\n", error_code);
        return 1;
    }
}

// ============================================================
// 使用说明
// ============================================================
static void print_usage(const char* prog) {
    std::fprintf(stderr,
        "Usage: %s [OPTIONS] [SN] [bizid] [msgid]\n"
        "\n"
        "Send a 12R SoC test request via MQTT and parse the 12A response.\n"
        "Follows the factory_proto binary protocol (38-byte request, 42-byte response).\n"
        "\n"
        "Positional arguments:\n"
        "  SN        Device serial number (max 16 chars, default: TEST000000000000)\n"
        "  bizid     Business ID        (max 16 chars, default: 0000000000000000)\n"
        "  msgid     Message ID         (max 8 chars,  default: 00000000)\n"
        "\n"
        "Options:\n"
        "  --broker, -b   MQTT broker address   (default: 127.0.0.1)\n"
        "  --port,   -p   MQTT broker port      (default: 1883)\n"
        "  --timeout,-t   Response wait timeout (default: 120 seconds)\n"
        "  --help,   -h   Show this help\n"
        "\n"
        "Error code bits:\n"
        "  bit 0 (0x0001): GPU  test failure\n"
        "  bit 1 (0x0002): DDR  test failure\n"
        "  bit 2 (0x0004): CPU  test failure\n"
        "  bit 3 (0x0008): NPU  test failure\n"
        "  bit 4 (0x0010): eMMC test failure\n"
        "\n"
        "Exit code: 0 = all pass, 1 = any failure or error\n",
        prog);
}

// ============================================================
// 命令行解析
// ============================================================
static bool parse_args(int argc, char** argv, AppState& st) {
    static const struct option long_opts[] = {
        {"broker",   required_argument, nullptr, 'b'},
        {"port",     required_argument, nullptr, 'p'},
        {"timeout",  required_argument, nullptr, 't'},
        {"help",     no_argument,       nullptr, 'h'},
        {nullptr,    0,                 nullptr, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "b:p:t:h", long_opts, nullptr)) != -1) {
        switch (opt) {
            case 'b': st.broker = optarg;       break;
            case 'p': st.port   = std::atoi(optarg); break;
            case 't': st.timeout_sec = std::atoi(optarg); break;
            case 'h': print_usage(argv[0]); return false;
            default:  print_usage(argv[0]); return false;
        }
    }

    // 剩余位置参数
    int pos = 0;
    for (int i = optind; i < argc; ++i) {
        switch (pos) {
            case 0: st.sn    = argv[i]; break;
            case 1: st.bizid = argv[i]; break;
            case 2: st.msgid = argv[i]; break;
            default: break;
        }
        ++pos;
    }

    return true;
}

// ============================================================
// main
// ============================================================
int main(int argc, char** argv) {
    AppState st;

    if (!parse_args(argc, argv, st)) {
        return 0;  // --help 或解析错误，已打印
    }

    // ---- 初始化 libmosquitto ----
    mosquitto_lib_init();

    st.mosq = mosquitto_new("soc_test_cli", true, &st);
    if (!st.mosq) {
        std::fprintf(stderr, "[soc_test] mosquitto_new failed\n");
        mosquitto_lib_cleanup();
        return 1;
    }

    // 注册回调
    mosquitto_connect_callback_set(st.mosq, on_connect_cb);
    mosquitto_disconnect_callback_set(st.mosq, on_disconnect_cb);
    mosquitto_message_callback_set(st.mosq, on_message_cb);

    // 连接 broker
    int rc = mosquitto_connect(st.mosq, st.broker.c_str(), st.port, 60);
    if (rc != MOSQ_ERR_SUCCESS) {
        std::fprintf(stderr, "[soc_test] connect failed: %s (rc=%d)\n",
                     mosquitto_strerror(rc), rc);
        mosquitto_destroy(st.mosq);
        mosquitto_lib_cleanup();
        return 1;
    }

    // 启动 MQTT loop (后台线程)
    rc = mosquitto_loop_start(st.mosq);
    if (rc != MOSQ_ERR_SUCCESS) {
        std::fprintf(stderr, "[soc_test] loop_start failed: %s (rc=%d)\n",
                     mosquitto_strerror(rc), rc);
        mosquitto_destroy(st.mosq);
        mosquitto_lib_cleanup();
        return 1;
    }

    // ---- 等待 12A 响应或超时 ----
    {
        std::unique_lock<std::mutex> lk(st.resp_mutex);
        bool got_response = st.resp_cv.wait_for(lk, std::chrono::seconds(st.timeout_sec),
                                                 [&st] { return st.resp_received; });

        st.done = true;

        if (!got_response) {
            std::fprintf(stderr, "[soc_test] TIMEOUT: no 12A response within %ds\n",
                         st.timeout_sec);
            st.exit_code = 1;
        } else if (st.resp_payload.empty()) {
            std::fprintf(stderr, "[soc_test] ERROR: empty response (disconnected?)\n");
            st.exit_code = 1;
        } else {
            st.exit_code = parse_and_print_error(st.resp_payload);
        }
    }

    // ---- 清理 ----
    mosquitto_disconnect(st.mosq);
    mosquitto_loop_stop(st.mosq, false);
    mosquitto_destroy(st.mosq);
    mosquitto_lib_cleanup();

    return st.exit_code;
}
