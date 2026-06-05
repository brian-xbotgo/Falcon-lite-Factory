#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace ft {

// Camera configuration for V4L2 recorder
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
    std::string  device      = "hw:1,0";   // ALSA device
    unsigned int sample_rate = 16000;
    unsigned int channels    = 1;
    int          gain_db     = 100;
};

struct RecorderConfig {
    std::vector<CameraConfig> cameras;
    AudioConfig  audio;
    unsigned int max_duration_sec = 0;   // 0 = unlimited
    std::string config_source;

    bool valid() const { return !cameras.empty(); }
};

struct RecorderCmd {
    uint8_t cmd          = 0;
    uint8_t cmd_origin   = 0;
    uint8_t ai_flag      = 0;
    uint8_t fence_flag   = 0;
};

class IRecorder {
public:
    virtual ~IRecorder() = default;
    virtual bool start(const RecorderCmd& cmd) = 0;
    virtual bool stop() = 0;
    virtual bool isRecording() const = 0;
};

} // namespace ft
