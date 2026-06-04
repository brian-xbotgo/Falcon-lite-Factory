#pragma once
#include <cstdint>
#include <cstddef>

namespace ft {

struct EncoderConfig {
    int width = 1280;
    int height = 720;
    int fps = 30;
    int bitrate_kbps = 2000;
    int gop = 60;
};

class IEncoder {
public:
    virtual ~IEncoder() = default;
    virtual bool init(const EncoderConfig& cfg) = 0;
    virtual void deinit() = 0;
    virtual bool isInitialized() const = 0;
    virtual bool encode(const uint8_t* nv12_data, size_t nv12_size,
                        const uint8_t** out_data, size_t* out_size) = 0;
};

} // namespace ft
