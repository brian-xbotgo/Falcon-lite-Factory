#include "ble/BleMqttBridge.h"
#include "ble/BleConstants.h"
#include "mqtt_def.h"
#include <mosquitto.h>
#include <cstdio>
#include <cstring>
#include <unistd.h>

#define LOG(fmt, ...) std::fprintf(stderr, "[ble_wifi] " fmt, ##__VA_ARGS__)

namespace ft {

// Static callbacks — forward to instance via userdata
static void mqtt_msg_cb(mosquitto*, void* ud, const mosquitto_message* msg)
{
    if (ud) static_cast<BleMqttBridge*>(ud)->onMessage(msg);
}

static void mqtt_conn_cb(mosquitto*, void* ud, int rc)
{
    if (ud) static_cast<BleMqttBridge*>(ud)->onConnect(rc);
}

BleMqttBridge::~BleMqttBridge()
{
    deinit();
}

int BleMqttBridge::init(bool factoryMode, bool waitForSn)
{
    m_factoryMode = factoryMode;

    mosquitto_lib_init();
    m_mosq = mosquitto_new(MQTT_CLIENT_ID, true, this);
    if (!m_mosq) {
        LOG("Failed to create mosquitto instance\n");
        mosquitto_lib_cleanup();
        return -1;
    }

    mosquitto_message_callback_set(m_mosq, mqtt_msg_cb);
    mosquitto_connect_callback_set(m_mosq, mqtt_conn_cb);

    if (mosquitto_connect(m_mosq, MQTT_HOST, MQTT_PORT, MQTT_KEEPALIVE) != MOSQ_ERR_SUCCESS) {
        LOG("Failed to connect to MQTT broker %s:%d\n", MQTT_HOST, MQTT_PORT);
        mosquitto_destroy(m_mosq);
        m_mosq = nullptr;
        mosquitto_lib_cleanup();
        return -1;
    }

    if (mosquitto_loop_start(m_mosq) != MOSQ_ERR_SUCCESS) {
        LOG("Failed to start mosquitto loop\n");
        mosquitto_destroy(m_mosq);
        m_mosq = nullptr;
        mosquitto_lib_cleanup();
        return -1;
    }

    // In factory mode, block until SN is received
    if (m_factoryMode && waitForSn) {
        while (!m_snValid) {
            LOG("Waiting for SN via MQTT...\n");
            sleep(1);
        }
    }

    return 0;
}

void BleMqttBridge::deinit()
{
    if (m_mosq) {
        mosquitto_loop_stop(m_mosq, false);
        mosquitto_destroy(m_mosq);
        m_mosq = nullptr;
    }
    mosquitto_lib_cleanup();
}

void BleMqttBridge::onMessage(const mosquitto_message* msg)
{
    if (!msg || !msg->payload) return;

    const char* payload = (const char*)msg->payload;
    int payloadlen = msg->payloadlen;

    // Extract SN from any test command payload: first 14 bytes
    // e.g. topic="17R", payload="11111111111111f648909b-af47-4419e0abfd" → SN="11111111111111"
    if (m_factoryMode && !m_snValid) {
        if (payloadlen >= SN_LEN) {
            memcpy(m_sn, payload, SN_LEN);
            m_snValid = true;
            LOG("Got SN from [%s]: %.*s\n", msg->topic, SN_LEN, m_sn);
            return;
        }
    }

    // Handle live status notification
    if (strcmp(msg->topic, MQTT_TOPIC_LIVE_NOTIFY) == 0) {
        unsigned char status = *(unsigned char*)msg->payload;
        if (status > 1) status = 1;
        setLiveStatus(status);
        return;
    }

    // Handle phone connect status
    if (strcmp(msg->topic, MQTT_TOPIC_PHONE_CONNECT_STATUS) == 0) {
        if (msg->payloadlen >= 1 && m_phoneConnectHandler) {
            unsigned char connected = *(unsigned char*)msg->payload;
            m_phoneConnectHandler(connected == 1);
        }
        return;
    }
}

void BleMqttBridge::onConnect(int rc)
{
    if (rc) { LOG("MQTT connect error %d\n", rc); return; }

    mosquitto_subscribe(m_mosq, nullptr, MQTT_TOPIC_LIVE_NOTIFY, MQTT_QOS);
    mosquitto_subscribe(m_mosq, nullptr, MQTT_TOPIC_PHONE_CONNECT_STATUS, MQTT_QOS);

    if (m_factoryMode) {
        // Subscribe to all topics — SN arrives with any test command
        mosquitto_subscribe(m_mosq, nullptr, "#", MQTT_QOS);
    }

    LOG("MQTT connected, subscribed to topics\n");
}

int BleMqttBridge::getLiveStatus()
{
    std::lock_guard<std::mutex> lock(m_liveMutex);
    return m_liveStatus;
}

void BleMqttBridge::setLiveStatus(int status)
{
    if (status > 1) status = 1;
    std::lock_guard<std::mutex> lock(m_liveMutex);
    m_liveStatus = status;
}

void BleMqttBridge::stopLive()
{
    if (!m_mosq) return;
    bool isStop = true;
    int rc = mosquitto_publish(m_mosq, nullptr, MQTT_TOPIC_LIVE_SESSION_STOP,
                               1, &isStop, 0, false);
    if (rc != MOSQ_ERR_SUCCESS)
        LOG("Failed to stop live: %s\n", mosquitto_strerror(rc));
}

int BleMqttBridge::publishAppToken(const char* token)
{
    if (!m_mosq || !token) return -1;
    int rc = mosquitto_publish(m_mosq, nullptr, MQTT_TOPIC_TOKEN_UPDATE,
                               strlen(token), token, 1, false);
    if (rc != MOSQ_ERR_SUCCESS) {
        LOG("Failed to publish token: %s\n", mosquitto_strerror(rc));
        return -1;
    }
    return 0;
}

void BleMqttBridge::getSn(uint8_t* buf)
{
    if (buf) memcpy(buf, m_sn, SN_LEN);
}

void BleMqttBridge::setPhoneConnectHandler(std::function<void(bool)> handler)
{
    m_phoneConnectHandler = std::move(handler);
}

} // namespace ft
