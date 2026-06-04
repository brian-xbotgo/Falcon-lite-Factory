#pragma once
#include "platforms/common/interface/IRecorder.h"

namespace ft {

class Rk3576Recorder : public IRecorder {
public:
    bool start(const RecorderCmd&) override { recording_ = true; return true; }
    bool stop() override { recording_ = false; return true; }
    bool isRecording() const override { return recording_; }
private:
    bool recording_ = false;
};

} // namespace ft
