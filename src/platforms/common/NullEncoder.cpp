#include "platforms/common/interface/IEncoder.h"

namespace ft {

class NullEncoder : public IEncoder {
public:
    bool init(const EncoderConfig&) override { return false; }
    void deinit() override {}
    bool isInitialized() const override { return false; }
    bool encode(const uint8_t*, size_t, const uint8_t**, size_t*) override { return false; }
};

} // namespace ft
