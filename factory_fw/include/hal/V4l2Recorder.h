#pragma once
#include "platforms/common/interface/IRecorder.h"
#include "platforms/common/interface/IEncoder.h"

namespace ft {

class V4l2Recorder : public IRecorder {
public:
    V4l2Recorder(IEncoder* encoder);
    bool start(const RecorderCmd& cmd) override;
    bool stop() override;
    bool isRecording() const override;

private:
    IEncoder* encoder_ = nullptr;
    bool recording_ = false;
};

} // namespace ft
