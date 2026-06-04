#pragma once
#include "platforms/common/interface/IEncoder.h"

namespace ft {

class RkMppEncoder : public IEncoder {
public:
    bool init(const EncoderConfig&) override {
        initialized_ = true;
        return true;
    }
    void deinit() override { initialized_ = false; }
    bool isInitialized() const override { return initialized_; }
    bool encode(const uint8_t*, size_t, const uint8_t** out_data, size_t* out_size) override {
        static uint8_t dummy = 0;
        *out_data = &dummy;
        *out_size = 0;
        return true;
    }
private:
    bool initialized_ = false;
};

} // namespace ft
