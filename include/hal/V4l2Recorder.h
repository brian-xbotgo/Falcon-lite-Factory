#pragma once
#include "hal/IRecorder.h"
#include "hal/MppEncoder.h"
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <memory>
#include <cstdio>

// V4L2-based recorder — platform-independent video capture via Linux V4L2 API.
// Supports multiple cameras simultaneously, each in its own thread.
// No Rockchip-specific dependencies.  Coexists with NullRecorder fallback.

namespace ft {

class V4l2Recorder : public IRecorder {
public:
    explicit V4l2Recorder(const RecorderConfig& cfg);
    ~V4l2Recorder() override;

    // ---- IRecorder interface ----
    bool start(const RecorderCmd& cmd) override;
    bool stop() override;
    bool isRecording() const override;

    // ---- helpers ----
    const RecorderConfig& config() const { return m_cfg; }

private:
    // Per-camera worker (heap-allocated, non-copyable due to std::atomic + std::thread)
    struct CameraWorker {
        CameraConfig    cfg;
        int             fd      = -1;
        FILE*           outfile = nullptr;
        std::thread     thread;
        std::atomic<bool> running{false};
        bool            mplane  = false;  // true if device uses V4L2 multi-plane API
        MppEncoder      encoder;
    };

    // V4L2 buffer type — auto-selects single vs multi-plane per device
    static unsigned int bufType(const CameraWorker& w);

    bool startCamera(CameraWorker& w);
    void cameraLoop(CameraWorker& w);
    void stopCamera(CameraWorker& w);
    void writeH264(CameraWorker& w, const uint8_t* data, size_t len);

    // V4L2 low-level helpers
    static bool v4l2Open(const std::string& device, CameraWorker& w);
    static bool v4l2SetFormat(CameraWorker& w);
    static bool v4l2SetFps(int fd, unsigned int fps);
    static bool v4l2ReqBufs(int fd, int count, unsigned int type);
    static bool v4l2StreamOn(int fd, unsigned int type);
    static bool v4l2StreamOff(int fd, unsigned int type);
    static unsigned int fourccFromString(const std::string& s);

    RecorderConfig                              m_cfg;
    std::vector<std::unique_ptr<CameraWorker>>  m_workers;
    std::atomic<bool>                           m_active{false};
};

// Factory
V4l2Recorder* createV4l2Recorder(const RecorderConfig& cfg);

} // namespace ft
