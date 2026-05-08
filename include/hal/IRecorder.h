#pragma once
#include <cstdint>

// Recording abstraction — 4-byte command protocol (compatible with multi_media ALR)
//   byte0: cmd      (0=start, 1=pause, 2=recover, 3=stop)
//   byte1: origin   (0=system)
//   byte2: ai_flag  (0=off)
//   byte3: fence_flag (0=off)

namespace ft {

struct RecorderCmd {
    uint8_t cmd          = 0;
    uint8_t cmd_origin   = 0;
    uint8_t ai_flag      = 0;
    uint8_t fence_flag   = 0;

    // Pack 4 bytes (network order compatible)
    void pack(uint8_t out[4]) const {
        out[0] = cmd;
        out[1] = cmd_origin;
        out[2] = ai_flag;
        out[3] = fence_flag;
    }

    static RecorderCmd startCmd()  { return {0, 0, 0, 0}; }
    static RecorderCmd stopCmd()   { return {3, 0, 0, 0}; }
};

class IRecorder {
public:
    virtual ~IRecorder() = default;
    virtual bool start(const RecorderCmd& cmd) = 0;
    virtual bool stop() = 0;
    virtual bool isRecording() const = 0;
};

} // namespace ft
