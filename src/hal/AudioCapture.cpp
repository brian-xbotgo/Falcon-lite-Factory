// AudioCapture — ALSA audio capture via arecord subprocess pipe.
// Uses arecord in raw PCM mode (S16_LE, configurable rate/channels).
// Avoids linking against libasound; relies on system arecord.

#include "hal/AudioCapture.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <cerrno>
#include <climits>
#include <cmath>
#include <signal.h>

namespace ft {

AudioCapture::~AudioCapture() {
    close();
}

bool AudioCapture::open(const std::string& device, unsigned int rate,
                        unsigned int channels, int gain_db)
{
    if (pipe_) {
        std::fprintf(stderr, "[audio] already open\n");
        return false;
    }

    sample_rate_ = rate;
    channels_    = channels;

    // ── 1) 设置 PDM 麦克风增益（用 gain_db 当 0~100 百分比，推荐 60~75）──
    {
        int gain_pct = gain_db;
        if (gain_pct < 0)   gain_pct = 0;
        if (gain_pct > 100) gain_pct = 100;

        char sset_cmd[256];
        std::snprintf(sset_cmd, sizeof(sset_cmd),
                      "amixer -c 1 sset 'PDM0 Gain Volume 0' %d%% 2>/dev/null",
                      gain_pct);
        int rc = std::system(sset_cmd);
        if (rc == 0) {
            std::fprintf(stderr, "[audio] set PDM0 Gain to %d%%\n", gain_pct);
        } else {
            std::fprintf(stderr, "[audio] warning: amixer rc=%d\n", rc);
        }
        usleep(100 * 1000);  // 等 PDM amp 稳定
    }

    // ── 2) 硬件参数（RV1126B PDM mic 只支持 S32_LE / 48k / 2ch）──
    hw_rate_        = 48000;
    hw_channels_    = 2;
    hw_format_bits_ = 32;

    // ── 3) 初始化抗混叠 LPF + DC blocker ★★★ 新增 ★★★ ──
    // 4 阶 Butterworth 用两级 Biquad 级联实现，截止 7 kHz
    lpf1_.setLowpass(static_cast<float>(hw_rate_), 1500.0f, 0.5412f);
    lpf2_.setLowpass(static_cast<float>(hw_rate_), 1500.0f, 1.3066f);
    dc_x1_ = 0.0f;
    dc_y1_ = 0.0f;

    // ── 4) 启动 arecord 子进程 ──
    char cmd[256];
    std::snprintf(cmd, sizeof(cmd),
                  "arecord -D %s -f S32_LE -r %u -c %u -t raw "
                  "--buffer-time=40000 2>/dev/null",
                  device.c_str(), hw_rate_, hw_channels_);

    std::fprintf(stderr,
                 "[audio] starting capture: %s (hw: S32_LE/%uch/%uHz, "
                 "sw: S16_LE/%uch/%uHz)\n",
                 cmd, hw_channels_, hw_rate_, channels, rate);

    pipe_ = ::popen(cmd, "r");
    if (!pipe_) {
        std::fprintf(stderr, "[audio] popen failed: %s\n", strerror(errno));
        return false;
    }

    ::setvbuf(pipe_, nullptr, _IONBF, 0);
    return true;
}

void AudioCapture::close()
{
    if (pipe_) {
        std::fprintf(stderr, "[audio] stopping capture\n");
        // pclose sends SIGTERM to arecord and waits
        int rc = ::pclose(pipe_);
        pipe_ = nullptr;
        if (rc != 0) {
            std::fprintf(stderr, "[audio] arecord exited with code %d\n", rc);
        }
    }
}

int AudioCapture::readFrame(int16_t* buf, size_t samples)
{
    if (!pipe_) return -1;

    const size_t hw_samples_needed = samples * 3;  // 48k -> 16k decimation
    const size_t hw_bytes_needed   =
        hw_samples_needed * hw_channels_ * (hw_format_bits_ / 8);

    // 1) 从 arecord 管道读原始 S32_LE/stereo/48k
    std::vector<uint8_t> hw_buf(hw_bytes_needed);
    size_t bytes_read = 0;
    while (bytes_read < hw_bytes_needed) {
        size_t n = ::fread(hw_buf.data() + bytes_read, 1,
                           hw_bytes_needed - bytes_read, pipe_);
        if (n == 0) {
            if (::feof(pipe_))  { std::fprintf(stderr, "[audio] EOF\n");   return -1; }
            if (::ferror(pipe_)){ std::fprintf(stderr, "[audio] err: %s\n",
                                               strerror(errno));            return -1; }
            continue;
        }
        bytes_read += n;
    }

    const auto* hw_ptr = reinterpret_cast<const int32_t*>(hw_buf.data());

    // 2) 对全部 48k 输入做：L+R 平均  →  级联 Biquad 低通
    //    然后每 3 个样本取 1 个作为 16k 输出（在低通后已不会混叠）
    //
    //    PDM 数据：24-bit 左对齐在 S32 容器里。算术右移 8 位得到 24-bit 有符号值，
    //    范围 [-2^23, 2^23-1]。最后再缩到 S16 范围（除以 256）。
    for (size_t i = 0; i < samples; ++i) {
        float decimated = 0.0f;

        for (size_t j = 0; j < 3; ++j) {
            const size_t base = (i * 3 + j) * hw_channels_;

            // L+R 平均（24-bit 域），保留全精度
            int32_t L = hw_ptr[base]     >> 8;   // -> 24-bit signed
            int32_t R = hw_ptr[base + 1] >> 8;
            float x = static_cast<float>(L + R) * 0.5f;

            // 抗混叠：两级 Biquad LPF（4 阶 Butterworth @ 7 kHz）
            float y = lpf2_.process(lpf1_.process(x));

            // 取最后一个作为该输出样本（已充分低通，可安全 decimation）
            decimated = y;
        }

        // 3) DC blocker（在 24-bit 域）
        float dc_out = decimated - dc_x1_ + DC_R * dc_y1_;
        dc_x1_ = decimated;
        dc_y1_ = dc_out;

        // 4) 24-bit -> 16-bit（除 256），饱和钳位
        int32_t s = static_cast<int32_t>(dc_out) >> 8;
        if (s >  32767) s =  32767;
        if (s < -32768) s = -32768;
        buf[i] = static_cast<int16_t>(s);
    }

    return static_cast<int>(samples);
}

// ── Biquad LPF 系数计算（RBJ Audio EQ Cookbook 标准公式）─────────────────────
void AudioCapture::Biquad::setLowpass(float fs, float fc, float Q) {
    const float w0    = 2.0f * 3.14159265358979f * fc / fs;
    const float cw    = std::cos(w0);
    const float sw    = std::sin(w0);
    const float alpha = sw / (2.0f * Q);
    const float a0    = 1.0f + alpha;

    b0 = (1.0f - cw) * 0.5f / a0;
    b1 = (1.0f - cw)        / a0;
    b2 = (1.0f - cw) * 0.5f / a0;
    a1 = -2.0f * cw         / a0;
    a2 = (1.0f - alpha)     / a0;
    z1 = z2 = 0.0f;
}

} // namespace ft
