#pragma once
#include <cstdint>
#include <cstddef>
#include <chrono>
// MppEncoder — Rockchip MPP H.264 hardware encoder wrapper.
// Feeds NV12 frames from V4L2 capture into the encoder and
// outputs H.264 Annex B elementary stream.

namespace ft {

struct MppEncoderConfig {
    int width        = 1280;
    int height       = 720;
    int fps          = 30;
    int bitrate_kbps = 2000;   // target bitrate in kbps
    int gop          = 60;     // keyframe interval (0 = single I, all P)
};

class MppEncoder {
public:
    MppEncoder() = default;
    ~MppEncoder();

    // Non-copyable (owns MPP context)
    MppEncoder(const MppEncoder&) = delete;
    MppEncoder& operator=(const MppEncoder&) = delete;

    bool init(const MppEncoderConfig& cfg);
    void deinit();
    bool isInitialized() const { return initialized_; }

    // Feed one NV12 frame (width×height×1.5 bytes), get H.264 output.
    // out_data / out_size point into internal buffer — valid until
    // the next call to encode() or deinit().
    // Returns false on error; caller should stop encoding on first failure.
    bool encode(const uint8_t* nv12_data, size_t nv12_size,
                const uint8_t** out_data, size_t* out_size);

private:
    void cleanupPacket();

    bool initialized_   = false;
    void* ctx_          = nullptr;  // MppCtx
    void* mpi_          = nullptr;  // MppApi*
    void* enc_cfg_      = nullptr;  // MppEncCfg
    void* buf_group_    = nullptr;  // MppBufferGroup (for NV12 input buffers)
    void* packet_       = nullptr;  // MppPacket (current output, kept alive)
    int   hor_stride_   = 0;
    int   ver_stride_   = 0;
    int   frame_size_   = 0;
    MppEncoderConfig cfg_;

    // —— 码率统计 ——
    uint64_t stat_bytes_  = 0;
    uint64_t stat_frames_ = 0;
    std::chrono::steady_clock::time_point stat_start_;
};

} // namespace ft
