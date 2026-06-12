#pragma once
#include "platforms/common/interface/IRecorder.h"
#include <memory>

namespace ft {

class V4l2Recorder;

class Rk3576Recorder : public IRecorder {
public:
    Rk3576Recorder();
    ~Rk3576Recorder() override;

    bool start(const RecorderCmd& cmd) override;
    bool stop() override;
    bool isRecording() const override;

private:
    RecorderConfig config_;
    std::unique_ptr<V4l2Recorder> recorder_;
};

} // namespace ft
