#include "platforms/common/interface/IRecorder.h"

namespace ft {

class BaseRecorder : public IRecorder {
public:
    bool start(const RecorderCmd&) override { return false; }
    bool stop() override { return false; }
    bool isRecording() const override { return false; }
};

} // namespace ft
