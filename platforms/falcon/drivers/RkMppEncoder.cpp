#include "drivers/RkMppEncoder.h"
#include <rockchip/rk_mpi.h>
#include <rockchip/mpp_buffer.h>
#include <rockchip/mpp_frame.h>
#include <rockchip/mpp_packet.h>
#include <rockchip/rk_venc_cfg.h>
#include <rockchip/rk_type.h>
#include <cstdio>
#include <cstring>

namespace ft {

RkMppEncoder::~RkMppEncoder() {
    deinit();
}

bool RkMppEncoder::init(const EncoderConfig& cfg) {
    cfg_ = cfg;

    MppCtx  raw_ctx  = nullptr;
    MppApi* raw_mpi  = nullptr;
    MPP_RET ret = mpp_create(&raw_ctx, &raw_mpi);
    if (ret != MPP_OK) {
        std::fprintf(stderr, "[mpp] mpp_create failed: %d\n", ret);
        return false;
    }
    ctx_ = raw_ctx;
    mpi_ = raw_mpi;

    ret = mpp_init(raw_ctx, MPP_CTX_ENC, MPP_VIDEO_CodingAVC);
    if (ret != MPP_OK) {
        std::fprintf(stderr, "[mpp] mpp_init(H.264) failed: %d\n", ret);
        deinit();
        return false;
    }

    MppEncCfg cfg_handle = nullptr;
    ret = mpp_enc_cfg_init(&cfg_handle);
    if (ret != MPP_OK) {
        std::fprintf(stderr, "[mpp] mpp_enc_cfg_init failed: %d\n", ret);
        deinit();
        return false;
    }
    enc_cfg_ = cfg_handle;

    hor_stride_ = ((cfg_.width  + 15) & ~15);
    ver_stride_ = ((cfg_.height + 15) & ~15);
    frame_size_ = hor_stride_ * ver_stride_ * 3 / 2;  // NV12

    mpp_enc_cfg_set_s32(cfg_handle, "prep:width",      cfg_.width);
    mpp_enc_cfg_set_s32(cfg_handle, "prep:height",     cfg_.height);
    mpp_enc_cfg_set_s32(cfg_handle, "prep:hor_stride", hor_stride_);
    mpp_enc_cfg_set_s32(cfg_handle, "prep:ver_stride", ver_stride_);
    mpp_enc_cfg_set_s32(cfg_handle, "prep:format",     MPP_FMT_YUV420SP);

    mpp_enc_cfg_set_s32(cfg_handle, "rc:mode",          1);                  // CBR
    mpp_enc_cfg_set_s32(cfg_handle, "rc:quality",       MPP_ENC_RC_QUALITY_MEDIUM);
    mpp_enc_cfg_set_s32(cfg_handle, "rc:bps_target",    cfg_.bitrate_kbps);  // unit: kbps
    mpp_enc_cfg_set_s32(cfg_handle, "rc:bps_max",       cfg_.bitrate_kbps * 17 / 16);
    mpp_enc_cfg_set_s32(cfg_handle, "rc:bps_min",       cfg_.bitrate_kbps * 15 / 16);
    mpp_enc_cfg_set_s32(cfg_handle, "rc:gop",           cfg_.gop);
    mpp_enc_cfg_set_s32(cfg_handle, "rc:fps_in_num",    cfg_.fps);
    mpp_enc_cfg_set_s32(cfg_handle, "rc:fps_in_denom",  1);
    mpp_enc_cfg_set_s32(cfg_handle, "rc:fps_out_num",   cfg_.fps);
    mpp_enc_cfg_set_s32(cfg_handle, "rc:fps_out_denom", 1);
    mpp_enc_cfg_set_s32(cfg_handle, "rc:qp_init",       24);
    mpp_enc_cfg_set_s32(cfg_handle, "rc:qp_min",        24);
    mpp_enc_cfg_set_s32(cfg_handle, "rc:qp_max",        40);
    mpp_enc_cfg_set_s32(cfg_handle, "rc:qp_min_i",      22);
    mpp_enc_cfg_set_s32(cfg_handle, "rc:qp_max_i",      40);
    mpp_enc_cfg_set_s32(cfg_handle, "rc:qp_max_step",   8);

    mpp_enc_cfg_set_s32(cfg_handle, "codec:type", MPP_VIDEO_CodingAVC);

    mpp_enc_cfg_set_s32(cfg_handle, "h264:profile",             100);  // High
    mpp_enc_cfg_set_s32(cfg_handle, "h264:level",               40);   // 1080p@30fps
    mpp_enc_cfg_set_s32(cfg_handle, "h264:cabac_en",      1);
    mpp_enc_cfg_set_s32(cfg_handle, "h264:trans8x8",      1);
    mpp_enc_cfg_set_s32(cfg_handle, "h264:qp_init",             24);
    mpp_enc_cfg_set_s32(cfg_handle, "h264:qp_min",              24);
    mpp_enc_cfg_set_s32(cfg_handle, "h264:qp_max",              40);
    mpp_enc_cfg_set_s32(cfg_handle, "h264:qp_min_i",            22);
    mpp_enc_cfg_set_s32(cfg_handle, "h264:qp_max_i",            40);
    mpp_enc_cfg_set_s32(cfg_handle, "h264:qp_max_step",         8);

    ret = raw_mpi->control(raw_ctx, MPP_ENC_SET_CFG, cfg_handle);
    if (ret != MPP_OK) {
        std::fprintf(stderr, "[mpp] MPP_ENC_SET_CFG failed: %d\n", ret);
        deinit();
        return false;
    }

    MppBufferGroup group = nullptr;
    ret = mpp_buffer_group_get(&group, MPP_BUFFER_TYPE_ION,
                               MPP_BUFFER_INTERNAL, "mpp_enc", __func__);
    if (ret != MPP_OK) {
        std::fprintf(stderr, "[mpp] mpp_buffer_group_get failed: %d\n", ret);
        deinit();
        return false;
    }
    buf_group_ = group;

    initialized_ = true;
    std::fprintf(stderr, "[mpp] encoder ready: %dx%d@%dfps H.264 CBR %dkbps GOP=%d stride=%dx%d\n",
                 cfg_.width, cfg_.height, cfg_.fps,
                 cfg_.bitrate_kbps, cfg_.gop,
                 hor_stride_, ver_stride_);
    return true;
}

void RkMppEncoder::deinit() {
    initialized_ = false;
    cleanupPacket();

    if (buf_group_) {
        mpp_buffer_group_put(static_cast<MppBufferGroup>(buf_group_));
        buf_group_ = nullptr;
    }
    if (enc_cfg_) {
        mpp_enc_cfg_deinit(static_cast<MppEncCfg>(enc_cfg_));
        enc_cfg_ = nullptr;
    }
    if (ctx_) {
        mpp_destroy(static_cast<MppCtx>(ctx_));
        ctx_  = nullptr;
        mpi_  = nullptr;
    }
}

bool RkMppEncoder::encode(const uint8_t* nv12_data, size_t nv12_size,
                          const uint8_t** out_data, size_t* out_size) {
    if (!initialized_) return false;

    MppCtx  raw_ctx  = static_cast<MppCtx>(ctx_);
    MppApi* raw_mpi  = static_cast<MppApi*>(mpi_);

    cleanupPacket();

    MppBuffer buf = nullptr;
    MPP_RET ret = mpp_buffer_get_with_tag(static_cast<MppBufferGroup>(buf_group_),
                                          &buf, frame_size_, "mpp_enc", __func__);
    if (ret != MPP_OK) {
        std::fprintf(stderr, "[mpp] mpp_buffer_get failed: %d\n", ret);
        return false;
    }

    (void)nv12_size;
    std::memcpy(mpp_buffer_get_ptr_with_caller(buf, __func__),
                nv12_data, frame_size_);

    MppFrame frame = nullptr;
    mpp_frame_init(&frame);
    mpp_frame_set_width(frame,      cfg_.width);
    mpp_frame_set_height(frame,     cfg_.height);
    mpp_frame_set_hor_stride(frame, hor_stride_);
    mpp_frame_set_ver_stride(frame, ver_stride_);
    mpp_frame_set_fmt(frame,        MPP_FMT_YUV420SP);
    mpp_frame_set_buffer(frame,     buf);
    mpp_frame_set_eos(frame,        0);

    ret = raw_mpi->encode_put_frame(raw_ctx, frame);
    mpp_frame_deinit(&frame);

    if (ret != MPP_OK) {
        std::fprintf(stderr, "[mpp] encode_put_frame failed: %d\n", ret);
        mpp_buffer_put_with_caller(buf, __func__);
        return false;
    }

    MppPacket raw_packet = nullptr;
    ret = raw_mpi->encode_get_packet(raw_ctx, &raw_packet);

    mpp_buffer_put_with_caller(buf, __func__);

    if (ret != MPP_OK) {
        std::fprintf(stderr, "[mpp] encode_get_packet failed: %d\n", ret);
        return false;
    }

    packet_ = raw_packet;
    *out_data = static_cast<const uint8_t*>(mpp_packet_get_data(raw_packet));
    *out_size = mpp_packet_get_length(raw_packet);

    if (stat_frames_ == 0) stat_start_ = std::chrono::steady_clock::now();
    stat_bytes_  += *out_size;
    stat_frames_ += 1;

    auto now = std::chrono::steady_clock::now();
    auto ms  = std::chrono::duration_cast<std::chrono::milliseconds>(
                   now - stat_start_).count();
    if (ms >= 5000) {
        double mbps = stat_bytes_ * 8.0 / 1000.0 / ms;
        double fps  = stat_frames_ * 1000.0 / ms;
        std::fprintf(stderr,
                     "[mpp-stat %p] %.2f Mbps  %.1f fps  (%llu frames in %lldms)\n",
                     (void*)this, mbps, fps,
                     (unsigned long long)stat_frames_, (long long)ms);
        stat_bytes_  = 0;
        stat_frames_ = 0;
        stat_start_  = now;
    }

    return true;
}

void RkMppEncoder::cleanupPacket() {
    if (packet_) {
        { MppPacket p = static_cast<MppPacket>(packet_); mpp_packet_deinit(&p); }
        packet_ = nullptr;
    }
}

} // namespace ft
