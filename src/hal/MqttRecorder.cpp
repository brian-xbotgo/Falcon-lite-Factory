// MqttRecorder — publishes 4-byte ALR command via factory's mosquitto connection.
// Single-process: Falcon_Air_Factory and multi_media share the same broker.
// If multi_media not present, messages are harmless (no subscriber).

#include "hal/IRecorder.h"
#include <cstdio>
#include <cstring>
#include <mosquitto.h>

namespace ft {
namespace {

class MqttRecorder : public IRecorder {
public:
    explicit MqttRecorder(mosquitto* m) : m_mosq(m) {}

    bool start(const RecorderCmd& cmd) override {
        uint8_t buf[4];
        cmd.pack(buf);
        return publish(buf, sizeof(buf));
    }

    bool stop() override {
        RecorderCmd cmd = RecorderCmd::stopCmd();
        uint8_t buf[4];
        cmd.pack(buf);
        return publish(buf, sizeof(buf));
    }

    bool isRecording() const override { return m_recording; }

private:
    bool publish(const uint8_t* data, size_t len) {
        if (!m_mosq) {
            std::fprintf(stderr, "[recorder] mqtt not set, skip\n");
            return false;
        }
        int rc = mosquitto_publish(m_mosq, nullptr, "ALR", len, data, 2, false);
        if (rc != MOSQ_ERR_SUCCESS) {
            std::fprintf(stderr, "[recorder] ALR publish err: %s\n", mosquitto_strerror(rc));
            return false;
        }
        std::fprintf(stderr, "[recorder] ALR cmd=%d published\n", data[0]);
        m_recording = (data[0] == 0);
        return true;
    }

    mosquitto* m_mosq;
    bool m_recording = false;
};

} // anonymous namespace

IRecorder* createMqttRecorder(void* mosq) {
    return new MqttRecorder(static_cast<mosquitto*>(mosq));
}

} // namespace ft
