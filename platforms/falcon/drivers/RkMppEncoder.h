#pragma once
#include "platforms/common/interface/IEncoder.h"
#include <cstdint>
#include <cstddef>
#include <chrono>

namespace ft {

class RkMppEncoder : public IEncoder {
public:
    RkMppEncoder() = default;
    ~RkMppEncoder() override;

    RkMppEncoder(const RkMppEncoder&) = delete;
    RkMppEncoder& operator=(const RkMppEncoder&) = delete;

    bool init(const EncoderConfig& cfg) override;
    void deinit() override;
    bool isInitialized() const override { return initialized_; }
    bool encode(const uint8_t* nv12_data, size_t nv12_size,
                const uint8_t** out_data, size_t* out_size) override;

private:
    void cleanupPacket();

    bool initialized_   = false;
    void* ctx_          = nullptr;  // MppCtx
    void* mpi_          = nullptr;  // MppApi*
    void* enc_cfg_      = nullptr;  // MppEncCfg
    void* buf_group_    = nullptr;  // MppBufferGroup
    void* packet_       = nullptr;  // MppPacket
    int   hor_stride_   = 0;
    int   ver_stride_   = 0;
    int   frame_size_   = 0;
    EncoderConfig cfg_;

    // 码率统计
    uint64_t stat_bytes_  = 0;
    uint64_t stat_frames_ = 0;
    std::chrono::steady_clock::time_point stat_start_;
};

} // namespace ft
