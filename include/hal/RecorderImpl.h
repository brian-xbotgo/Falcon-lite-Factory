#pragma once
#include "hal/IRecorder.h"
#include "hal/V4l2Recorder.h"

namespace ft {

// Default no-op recorder (factory firmware default)
IRecorder& nullRecorder();

// V4L2-based recorder — captures from /dev/videoX via standard Linux V4L2 API
V4l2Recorder* createV4l2Recorder(const RecorderConfig& cfg);

} // namespace ft
