#include "core/TestEngine.h"
#include "core/AsyncTaskQueue.h"
#include "hal/GpioController.h"
#include <mosquitto.h>
#include <cstdio>
#include <cstring>
#include <thread>
#include <chrono>

namespace ft {

// ========= MsgQueue =========
void MsgQueue::push(std::string topic, std::string payload) {
    std::lock_guard<std::mutex> lk(mu_);
    if (closed_) return;
    q_.push({std::move(topic), std::move(payload)});
    cv_.notify_one();
}

MsgQueue::Msg MsgQueue::pop() {
    std::unique_lock<std::mutex> lk(mu_);
    cv_.wait(lk, [this] { return !q_.empty() || closed_; });
    if (closed_ && q_.empty()) return {};
    Msg m = std::move(q_.front());
    q_.pop();
    return m;
}

void MsgQueue::close() {
    std::lock_guard<std::mutex> lk(mu_);
    closed_ = true;
    cv_.notify_all();
}

// ========= TestEngine =========
TestEngine::TestEngine()  { mosquitto_lib_init(); }
TestEngine::~TestEngine() { stop(); stopMqtt(); mosquitto_lib_cleanup(); }

void TestEngine::on_connect_cb(struct mosquitto*, void* ud, int rc) {
    auto* self = static_cast<TestEngine*>(ud);
    self->mqtt_connected_ = (rc == 0);
    if (rc == 0) {
        std::fprintf(stderr, "[mqtt] connected\n");
        mosquitto_subscribe(self->mosq_, nullptr, "#", 2);
    } else {
        std::fprintf(stderr, "[mqtt] connect failed: rc=%d, will retry\n", rc);
    }
}

void TestEngine::on_message_cb(struct mosquitto*, void* ud, const struct mosquitto_message* msg) {
    auto* self = static_cast<TestEngine*>(ud);
    std::string topic(msg->topic);
    std::string payload(static_cast<const char*>(msg->payload), msg->payloadlen);
    self->cmd_queue_.push(std::move(topic), std::move(payload));
}

int TestEngine::startMqtt(const std::string& clientId) {
    mosq_ = mosquitto_new(clientId.c_str(), true, this);
    if (!mosq_) return -1;
    mosquitto_connect_callback_set(mosq_, on_connect_cb);
    mosquitto_message_callback_set(mosq_, on_message_cb);
    mosquitto_reconnect_delay_set(mosq_, 1, 15, true);
    int rc = mosquitto_connect(mosq_, "127.0.0.1", 1883, 60);
    if (rc != MOSQ_ERR_SUCCESS) { std::fprintf(stderr, "[mqtt] connect rc=%d\n", rc); return rc; }
    rc = mosquitto_loop_start(mosq_);
    if (rc != MOSQ_ERR_SUCCESS) { std::fprintf(stderr, "[mqtt] loop_start rc=%d\n", rc); return rc; }
    return 0;
}

void TestEngine::stopMqtt() {
    if (mosq_) {
        mosquitto_disconnect(mosq_);
        mosquitto_loop_stop(mosq_, false);
        mosquitto_destroy(mosq_);
        mosq_ = nullptr;
    }
}

int TestEngine::publish(const std::string& topic, const std::string& payload, int qos, bool retain) {
    if (!mosq_) return -1;
    int rc = mosquitto_publish(mosq_, nullptr, topic.c_str(), payload.size(), payload.data(), qos, retain);
    if (rc != MOSQ_ERR_SUCCESS) {
        std::fprintf(stderr, "[engine] publish %s failed: %d\n", topic.c_str(), rc);
    }
    return rc;
}

bool TestEngine::isMqttConnected() const { return mqtt_connected_; }

void TestEngine::registerTest(const std::string& req_topic, StdTestHandler test_fn, bool async) {
    std::string resp_topic = req_to_resp_topic(req_topic);
    auto* engine = this;
    RawHandler wrapped = [engine, test_fn, resp_topic](const uint8_t* data, size_t len) {
        ProtoHeader hdr;
        if (!hdr.parse(data, len)) {
            std::fprintf(stderr, "[engine] short payload on %s\n", resp_topic.c_str());
            return;
        }
        save_sn(hdr.sn);
        uint32_t error_code = test_fn(hdr);
        if (!resp_topic.empty()) {
            std::string resp = hdr.build_response(error_code);
            engine->publish(resp_topic, resp, 2);
        }
    };
    registerRaw(req_topic, std::move(wrapped), async);
}

void TestEngine::registerRaw(const std::string& req_topic, RawHandler handler, bool async) {
    std::lock_guard<std::mutex> lk(routes_mu_);
    routes_[req_topic] = CmdEntry{req_topic, std::move(handler), async};
}

void TestEngine::start() {
    task_queue_ = new AsyncTaskQueue(2);
    if (!running_.exchange(true)) {
        thread_ = std::thread(&TestEngine::threadLoop, this);
    }
}

void TestEngine::stop() {
    if (!running_.exchange(false)) return;
    cmd_queue_.close();
    if (thread_.joinable()) thread_.join();
    if (task_queue_) { task_queue_->stop(); delete task_queue_; task_queue_ = nullptr; }
}

void TestEngine::threadLoop() {
    while (running_) {
        auto msg = cmd_queue_.pop();
        if (!running_) break;
        handleOne(msg.topic, msg.payload);
    }
}

void TestEngine::handleOne(const std::string& topic, const std::string& payload) {
    const CmdEntry* entry = nullptr;
    {
        std::lock_guard<std::mutex> lk(routes_mu_);
        auto it = routes_.find(topic);
        if (it == routes_.end()) return;
        entry = &it->second;
    }

    auto* data = reinterpret_cast<const uint8_t*>(payload.data());
    size_t len = payload.size();

    if (entry->async && task_queue_) {
        auto handler_copy = entry->handler;
        std::vector<uint8_t> payload_copy(data, data + len);
        WorkerTask task;
        task.work = [handler_copy, payload_copy]() {
            handler_copy(payload_copy.data(), payload_copy.size());
        };
        if (task_queue_->submit(std::move(task)) != 0) {
            std::fprintf(stderr, "[engine] worker busy, dropping %s\n", topic.c_str());
        }
        return;
    }

    try {
        entry->handler(data, len);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[engine] handler exception on %s: %s\n", topic.c_str(), e.what());
    }
}

void TestEngine::startLedThread() {
    std::thread([]() {
        while (globals().running) {
            int mode = globals().led_mode.load();
            switch (mode) {
            case MODE_NORMAL:
                GpioController::write(144, 0); GpioController::write(89, 1);
                std::this_thread::sleep_for(std::chrono::seconds(1));
                GpioController::write(89, 0);  GpioController::write(144, 1);
                std::this_thread::sleep_for(std::chrono::seconds(1));
                break;
            case MODE_TEST:
                GpioController::write(144, 0);
                GpioController::write(89, 1);
                std::this_thread::sleep_for(std::chrono::seconds(1));
                GpioController::write(89, 0);
                std::this_thread::sleep_for(std::chrono::seconds(1));
                break;
            case MODE_TEST_DONE:
                GpioController::write(144, 0); GpioController::write(89, 1);
                std::this_thread::sleep_for(std::chrono::seconds(1));
                break;
            case MODE_TEST_FAIL:
                GpioController::write(89, 0);
                GpioController::write(144, 1);
                std::this_thread::sleep_for(std::chrono::seconds(1));
                GpioController::write(144, 0);
                std::this_thread::sleep_for(std::chrono::seconds(1));
                break;
            default:
                std::this_thread::sleep_for(std::chrono::seconds(1));
                break;
            }
        }
    }).detach();
}

} // namespace ft
