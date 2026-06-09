#include "ble/BleMqttBridge.h"
#include "ble/BleConstants.h"
#include "mqtt_def.h"
#include <mosquitto.h>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <sys/stat.h>

#define LOG(fmt, ...) std::fprintf(stderr, "[ble_wifi] " fmt, ##__VA_ARGS__)

namespace ft {

namespace {

constexpr int kVersionInfoSnPcbaOffset = 4 + 16;
constexpr int kVersionInfoMinLen = kVersionInfoSnPcbaOffset + SN_LEN;

std::string snToString(const uint8_t* sn)
{
    return std::string(reinterpret_cast<const char*>(sn),
                       reinterpret_cast<const char*>(sn) + SN_LEN);
}

} // namespace

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

    m_mosq = mosquitto_new(MQTT_CLIENT_ID, true, this);
    if (!m_mosq) {
        LOG("Failed to create mosquitto instance\n");
        return -1;
    }

    mosquitto_message_callback_set(m_mosq, mqtt_msg_cb);
    mosquitto_connect_callback_set(m_mosq, mqtt_conn_cb);

    if (mosquitto_connect(m_mosq, MQTT_HOST, MQTT_PORT, MQTT_KEEPALIVE) != MOSQ_ERR_SUCCESS) {
        LOG("Failed to connect to MQTT broker %s:%d\n", MQTT_HOST, MQTT_PORT);
        mosquitto_destroy(m_mosq);
        m_mosq = nullptr;
        return -1;
    }

    if (mosquitto_loop_start(m_mosq) != MOSQ_ERR_SUCCESS) {
        LOG("Failed to start mosquitto loop\n");
        mosquitto_destroy(m_mosq);
        m_mosq = nullptr;
        return -1;
    }

    if (m_factoryMode && waitForSn) {
        loadSnFromFile();
        std::unique_lock<std::mutex> lock(m_snMutex);
        while (!m_snValid) {
            LOG("Waiting for 14-byte SN via MQTT topic AZA/sn_pcba before BLE advertising...\n");
            m_snCv.wait_for(lock, std::chrono::seconds(1));
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
}

void BleMqttBridge::onMessage(const mosquitto_message* msg)
{
    if (!msg || !msg->payload) return;

    const auto* payload = static_cast<const uint8_t*>(msg->payload);
    const int payloadLen = msg->payloadlen;

    if (m_factoryMode) {
        extractSnFromMessage(msg->topic, payload, payloadLen);
    }

    if (std::strcmp(msg->topic, MQTT_TOPIC_LIVE_NOTIFY) == 0) {
        unsigned char status = *static_cast<unsigned char*>(msg->payload);
        if (status > 1) status = 1;
        setLiveStatus(status);
        return;
    }

    if (std::strcmp(msg->topic, MQTT_TOPIC_PHONE_CONNECT_STATUS) == 0) {
        if (msg->payloadlen >= 1 && m_phoneConnectHandler) {
            unsigned char connected = *static_cast<unsigned char*>(msg->payload);
            m_phoneConnectHandler(connected == 1);
        }
        return;
    }
}

void BleMqttBridge::onConnect(int rc)
{
    if (rc) {
        LOG("MQTT connect error %d\n", rc);
        return;
    }

    mosquitto_subscribe(m_mosq, nullptr, MQTT_TOPIC_LIVE_NOTIFY, MQTT_QOS);
    mosquitto_subscribe(m_mosq, nullptr, MQTT_TOPIC_PHONE_CONNECT_STATUS, MQTT_QOS);

    if (m_factoryMode) {
        mosquitto_subscribe(m_mosq, nullptr, MQTT_TOPIC_VERSION_RESP, MQTT_QOS);
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
    if (!buf) return;
    std::lock_guard<std::mutex> lock(m_snMutex);
    memcpy(buf, m_sn, SN_LEN);
}

bool BleMqttBridge::hasValidSn() const
{
    std::lock_guard<std::mutex> lock(m_snMutex);
    return m_snValid;
}

bool BleMqttBridge::extractSnFromMessage(const char* topic, const uint8_t* payload,
                                         int payloadLen)
{
    if (!topic || !payload || payloadLen < SN_LEN) {
        return false;
    }

    if (std::strcmp(topic, MQTT_TOPIC_VERSION_RESP) == 0 &&
        payloadLen >= kVersionInfoMinLen &&
        updateSn(payload + kVersionInfoSnPcbaOffset, MQTT_TOPIC_VERSION_RESP)) {
        return true;
    }

    if (std::strcmp(topic, MQTT_TOPIC_VERSION_RESP) == 0) {
        LOG("Ignore AZA payload_len=%d, expected at least %d bytes for version_info.sn_pcba\n",
            payloadLen, kVersionInfoMinLen);
        return false;
    }

    return updateSn(payload, topic);
}

bool BleMqttBridge::updateSn(const uint8_t* sn, const char* source)
{
    if (!isValidSn(sn)) {
        return false;
    }

    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(m_snMutex);
        changed = !m_snValid || std::memcmp(m_sn, sn, SN_LEN) != 0;
        if (!changed) {
            return true;
        }
        std::memcpy(m_sn, sn, SN_LEN);
        m_snValid = true;
    }

    persistSn(sn);
    m_snCv.notify_all();
    const auto text = snToString(sn);
    LOG("Got 14-byte SN from [%s]: %s\n", source ? source : "<unknown>", text.c_str());
    return true;
}

bool BleMqttBridge::isValidSn(const uint8_t* sn)
{
    if (!sn) {
        return false;
    }

    bool hasNonZero = false;
    for (int i = 0; i < SN_LEN; ++i) {
        const auto ch = sn[i];
        if (ch == 0 || ch == 0xff || !std::isalnum(ch)) {
            return false;
        }
        if (ch != '0') {
            hasNonZero = true;
        }
    }
    return hasNonZero;
}

void BleMqttBridge::persistSn(const uint8_t* sn)
{
    if (!sn) {
        return;
    }

    mkdir("/device_data", 0755);
    const auto text = snToString(sn);
    {
        std::ifstream existing(SN_FILE);
        std::string line;
        while (std::getline(existing, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (line == text) {
                return;
            }
        }
    }

    std::ofstream out(SN_FILE, std::ios::app);
    if (out.is_open()) {
        out << text << '\n';
    }
}

bool BleMqttBridge::loadSnFromFile()
{
    std::ifstream in(SN_FILE);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.size() < SN_LEN) {
            continue;
        }
        if (updateSn(reinterpret_cast<const uint8_t*>(line.data()), SN_FILE)) {
            LOG("Loaded SN from %s\n", SN_FILE);
            return true;
        }
    }
    return false;
}

void BleMqttBridge::setPhoneConnectHandler(std::function<void(bool)> handler)
{
    m_phoneConnectHandler = std::move(handler);
}

} // namespace ft
