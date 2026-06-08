#pragma once
#include "platforms/common/interface/IRecorder.h"
#include "platforms/common/interface/IEncoder.h"
#include "control/Mp4Muxer.h"
#include "control/AudioCapture.h"
#include <functional>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <memory>
#include <cstdio>

// V4L2-based recorder — platform-independent video capture via Linux V4L2 API.
// Supports multiple cameras simultaneously, each in its own thread.
// Includes ALSA audio capture + G.711A encoding + MP4 muxing for the first camera.
// No Rockchip-specific dependencies.  Coexists with BaseRecorder fallback.

namespace ft {

class V4l2Recorder : public IRecorder {
public:
    using EncoderFactory = std::function<std::unique_ptr<IEncoder>()>;

    explicit V4l2Recorder(const RecorderConfig& cfg);
    V4l2Recorder(const RecorderConfig& cfg, EncoderFactory encoderFactory);
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
        std::thread     thread;
        std::atomic<bool> running{false};
        bool            mplane  = false;  // true if device uses V4L2 multi-plane API
        std::unique_ptr<IEncoder> encoder;
        Mp4Muxer        muxer;            // replaces raw fwrite — writes .mp4
        bool            hasAudio = false;  // true for the first camera (binds audio)
    };

    // V4L2 buffer type — auto-selects single vs multi-plane per device
    static unsigned int bufType(const CameraWorker& w);

    bool startCamera(CameraWorker& w, bool isFirst);
    void cameraLoop(CameraWorker& w);
    void stopCamera(CameraWorker& w, bool isFirst);
    void audioLoop();  // audio capture + G.711A encode + muxer feed

    // V4L2 low-level helpers
    static bool v4l2Open(const std::string& device, CameraWorker& w);
    static bool v4l2SetFormat(CameraWorker& w);
    static bool v4l2SetFps(int fd, unsigned int fps);
    static bool v4l2ReqBufs(int fd, int count, unsigned int type);
    static bool v4l2StreamOn(int fd, unsigned int type);
    static bool v4l2StreamOff(int fd, unsigned int type);
    static unsigned int fourccFromString(const std::string& s);

    RecorderConfig                              m_cfg;
    EncoderFactory                              m_encoderFactory;
    std::vector<std::unique_ptr<CameraWorker>>  m_workers;
    std::atomic<bool>                           m_active{false};

    // Audio (shared across cameras — only one mic, bound to worker[0])
    AudioCapture                  m_audioCapture;
    std::thread                   m_audioThread;
    std::atomic<bool>             m_audioRunning{false};
};

// Factory
V4l2Recorder* createV4l2Recorder(const RecorderConfig& cfg);
V4l2Recorder* createV4l2Recorder(const RecorderConfig& cfg,
                                 V4l2Recorder::EncoderFactory encoderFactory);

} // namespace ft
