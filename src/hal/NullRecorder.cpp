// NullRecorder — default no-op implementation.  Used when no real recorder is linked.

#include "hal/IRecorder.h"
#include <cstdio>

namespace ft {
namespace {

class NullRecorder : public IRecorder {
public:
    bool start(const RecorderCmd&) override {
        std::fprintf(stderr, "[recorder] null: start skipped (no recorder linked)\n");
        return true;
    }
    bool stop() override {
        std::fprintf(stderr, "[recorder] null: stop skipped\n");
        return true;
    }
    bool isRecording() const override { return false; }
};

static NullRecorder g_null;

} // anonymous namespace

IRecorder& nullRecorder() { return g_null; }

} // namespace ft
