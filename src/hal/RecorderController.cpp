#include "hal/RecorderController.h"

namespace ft {

RecorderController& RecorderController::instance() {
    static RecorderController inst;
    return inst;
}

void RecorderController::setRecorder(IRecorder* recorder) {
    recorder_ = recorder;
}

IRecorder* RecorderController::recorder() const {
    return recorder_;
}

} // namespace ft
