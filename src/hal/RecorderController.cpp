// RecorderController — HAL Singleton.  Delegates to injected IRecorder.

#include "hal/RecorderController.h"
#include "hal/RecorderImpl.h"
#include <cstdio>

namespace ft {

RecorderController& RecorderController::instance() {
    static RecorderController c;
    return c;
}

void RecorderController::setRecorder(IRecorder* r) {
    m_recorder = r;
}

bool RecorderController::start() {
    if (!m_recorder) {
        std::fprintf(stderr, "[recorder] no implementation set, using null\n");
        m_recorder = &nullRecorder();
    }
    return m_recorder->start(RecorderCmd::startCmd());
}

bool RecorderController::stop() {
    if (!m_recorder) return false;
    return m_recorder->stop();
}

bool RecorderController::isRecording() const {
    return m_recorder ? m_recorder->isRecording() : false;
}

} // namespace ft
