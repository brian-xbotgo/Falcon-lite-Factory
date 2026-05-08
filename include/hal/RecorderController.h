#pragma once

// HAL Singleton — aging calls this; real recorder injected at startup.
//   工厂固件: inject NullRecorder or MqttRecorder
//   正式固件: inject MediaRecorder (multi_media in same process)

#include "hal/IRecorder.h"
#include <cstdint>

namespace ft {

class RecorderController {
public:
    static RecorderController& instance();

    // Inject implementation (call once before any start/stop)
    void setRecorder(IRecorder* r);

    bool start();
    bool stop();
    bool isRecording() const;

private:
    RecorderController() = default;
    IRecorder* m_recorder = nullptr;
};

} // namespace ft
