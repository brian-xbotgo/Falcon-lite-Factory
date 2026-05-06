#pragma once

#include <string>
#include <functional>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <atomic>
#include <queue>
#include <condition_variable>
#include <cstdint>

#include "common/Types.h"

struct mosquitto;
struct mosquitto_message;

namespace ft {

class AsyncTaskQueue;

using StdTestHandler = std::function<uint32_t(const ProtoHeader& hdr)>;
using RawHandler = std::function<void(const uint8_t* data, size_t len)>;

struct CmdEntry {
    std::string req_topic;
    RawHandler  handler;
    bool        async = false;
};

// 线程安全的消息队列，用于 MQTT → TestEngine 通信
class MsgQueue {
public:
    struct Msg { std::string topic; std::string payload; };
    void push(std::string topic, std::string payload);
    Msg pop();           // 阻塞
    void close();
private:
    std::mutex mu_;
    std::condition_variable cv_;
    std::queue<Msg> q_;
    bool closed_ = false;
};

class TestEngine {
public:
    TestEngine();
    ~TestEngine();

    TestEngine(const TestEngine&) = delete;
    TestEngine& operator=(const TestEngine&) = delete;

    // MQTT 客户端控制 (内部封装 mosquitto)
    int  startMqtt(const std::string& clientId = "prodTest");
    void stopMqtt();
    int  publish(const std::string& topic, const std::string& payload, int qos = 2, bool retain = false);
    bool isMqttConnected() const;

    // 命令注册
    void registerTest(const std::string& req_topic, StdTestHandler handler, bool async = false);
    void registerRaw(const std::string& req_topic, RawHandler handler, bool async = false);

    // 生命周期
    void start();
    void stop();

    // 暴露消息队列供 MQTT 回调使用
    MsgQueue& cmdQueue() { return cmd_queue_; }

    // LED 线程
    void startLedThread();

private:
    void threadLoop();
    void handleOne(const std::string& topic, const std::string& payload);

    // MQTT
    struct mosquitto* mosq_ = nullptr;
    std::atomic<bool> mqtt_connected_{false};
    static void on_connect_cb(struct mosquitto*, void* ud, int rc);
    static void on_message_cb(struct mosquitto*, void* ud, const struct mosquitto_message* msg);

    MsgQueue    cmd_queue_;
    AsyncTaskQueue* task_queue_ = nullptr;

    std::unordered_map<std::string, CmdEntry> routes_;
    std::mutex routes_mu_;

    std::thread      thread_;
    std::atomic<bool> running_{false};
};

} // namespace ft
