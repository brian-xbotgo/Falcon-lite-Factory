#pragma once
#include "platforms/common/interface/IRecorder.h"

namespace ft {

class RecorderController {
public:
    static RecorderController& instance();
    void setRecorder(IRecorder* recorder);
    IRecorder* recorder() const;

private:
    IRecorder* recorder_ = nullptr;
};

} // namespace ft
