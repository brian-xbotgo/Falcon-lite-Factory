#include "drivers/Rk3576Recorder.h"
#include "hal/V4l2Recorder.h"

namespace ft {

Rk3576Recorder::Rk3576Recorder()
{
    RecorderConfig cfg;
    cfg.cameras.push_back({"/dev/video0", 1280, 720, "MJPG", 30, "/tmp/cam0.mp4", 4});
    cfg.audio.enabled = true;
    recorder_.reset(createV4l2Recorder(cfg));
}

Rk3576Recorder::~Rk3576Recorder() = default;

bool Rk3576Recorder::start(const RecorderCmd& cmd)
{
    if (!recorder_) return false;
    return recorder_->start(cmd);
}

bool Rk3576Recorder::stop()
{
    if (!recorder_) return false;
    return recorder_->stop();
}

bool Rk3576Recorder::isRecording() const
{
    if (!recorder_) return false;
    return recorder_->isRecording();
}

} // namespace ft
