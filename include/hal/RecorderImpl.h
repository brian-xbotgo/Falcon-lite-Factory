#pragma once
#include "hal/IRecorder.h"

namespace ft {

// Default no-op recorder (factory firmware default)
IRecorder& nullRecorder();

// MQTT-based recorder — publishes ALR via factory's mosquitto (single-process)
IRecorder* createMqttRecorder(void* mosq);

} // namespace ft
