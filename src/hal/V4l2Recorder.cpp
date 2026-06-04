#include "hal/V4l2Recorder.h"

namespace ft {

V4l2Recorder::V4l2Recorder(IEncoder* encoder) : encoder_(encoder) {}

bool V4l2Recorder::start(const RecorderCmd& cmd) {
    // TODO: implement V4L2 capture + encode
    recording_ = true;
    return true;
}

bool V4l2Recorder::stop() {
    recording_ = false;
    return true;
}

bool V4l2Recorder::isRecording() const {
    return recording_;
}

} // namespace ft
