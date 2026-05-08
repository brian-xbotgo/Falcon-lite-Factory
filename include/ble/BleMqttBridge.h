#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <functional>

struct mosquitto;
struct mosquitto_message;

namespace ft {

class BleMqttBridge {
public:
    BleMqttBridge() = default;
    ~BleMqttBridge();

    // Non-copyable
    BleMqttBridge(const BleMqttBridge&) = delete;
    BleMqttBridge& operator=(const BleMqttBridge&) = delete;

    // Initialize MQTT: connect to broker, subscribe topics, start loop thread.
    // If factory mode and waitForSn=true, blocks until SN received.
    int init(bool factoryMode, bool waitForSn = false);

    // Clean up MQTT resources.
    void deinit();

    // Get/set live status (thread-safe).
    int  getLiveStatus();
    void setLiveStatus(int status);

    // Publish: stop live session.
    void stopLive();

    // Publish: app token update.
    int publishAppToken(const char* token);

    // Copy stored SN to buf (SN_LEN bytes).
    void getSn(uint8_t* buf);
    const uint8_t* getSnBuf() const { return m_sn; }

    // Check if valid SN has been received.
    bool hasValidSn() const { return m_snValid; }

    // Register callback for phone connect status changes.
    // connected=true when phone connects to AP, false when disconnects.
    void setPhoneConnectHandler(std::function<void(bool)> handler);

    // MQTT callbacks (public for C callback access)
    void onMessage(const mosquitto_message* msg);
    void onConnect(int rc);

private:
    struct mosquitto* m_mosq = nullptr;

    // Live status
    int m_liveStatus = 1;  // 0=live, 1=not live
    std::mutex m_liveMutex;

    // Factory mode SN
    uint8_t m_sn[14] = {};
    bool m_snValid = false;
    bool m_factoryMode = false;

    // Phone connect handler
    std::function<void(bool)> m_phoneConnectHandler;
};

} // namespace ft
