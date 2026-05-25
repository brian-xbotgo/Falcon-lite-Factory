#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Recording abstraction — 4-byte command protocol (compatible with multi_media ALR)
//   byte0: cmd      (0=start, 1=pause, 2=recover, 3=stop)
//   byte1: origin   (0=system)
//   byte2: ai_flag  (0=off)
//   byte3: fence_flag (0=off)

namespace ft {

// ─── V4L2 camera configuration ──────────────────────────────────────────────

struct CameraConfig {
    std::string device;       // e.g. /dev/video0
    unsigned int width  = 1280;
    unsigned int height = 720;
    std::string format = "MJPG";  // FOURCC string: "MJPG", "YUYV", "H264"
    unsigned int fps    = 30;
    std::string output;       // output file path, e.g. /tmp/cam0.mp4
    int buffer_count    = 4;  // V4L2 buffer count for mmap streaming
};

struct AudioConfig {
    bool         enabled     = true;
    std::string  device      = "hw:1,0";   // ALSA device, e.g. PDM mic array
    unsigned int sample_rate = 16000;
    unsigned int channels    = 1;
    int          gain_db     = 100;         // microphone gain in dB (0 = no change)
};

struct RecorderConfig {
    std::vector<CameraConfig> cameras;
    AudioConfig  audio;
    unsigned int max_duration_sec = 0;   // 0 = unlimited, record until stop()
    std::string config_source;          // path to JSON config (for reference)

    bool valid() const { return !cameras.empty(); }
};
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
