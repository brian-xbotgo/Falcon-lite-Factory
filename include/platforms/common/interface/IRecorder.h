#pragma once
#include <cstdint>

namespace ft {

struct RecorderCmd {
    uint8_t cmd = 0;
    uint8_t cmd_origin = 0;
    uint8_t ai_flag = 0;
    uint8_t fence_flag = 0;
};

class IRecorder {
public:
    virtual ~IRecorder() = default;
    virtual bool start(const RecorderCmd& cmd) = 0;
    virtual bool stop() = 0;
    virtual bool isRecording() const = 0;
};

} // namespace ft
