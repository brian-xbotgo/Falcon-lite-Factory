#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <cstdio>

// ALSA audio capture via arecord subprocess pipe.
// Captures S16_LE PCM from the specified ALSA device.
// Read raw PCM frames in a blocking manner.

namespace ft {

class AudioCapture {
public:
    AudioCapture() = default;
    ~AudioCapture();

    // Non-copyable (owns pipe)
    AudioCapture(const AudioCapture&) = delete;
    AudioCapture& operator=(const AudioCapture&) = delete;

    // Start capture from the given ALSA device (e.g. "hw:1,0").
    // rate:     sample rate in Hz (16000 recommended)
    // channels: number of channels (1 = mono)
    // gain_db:  microphone gain in dB (0 = no change, typical range 0~30)
    // Returns false if arecord fails to start.
    bool open(const std::string& device, unsigned int rate, unsigned int channels,
              int gain_db = 0);

    // Close the capture pipe and terminate arecord.
    void close();

    // Read exactly 'samples' S16_LE PCM samples.
    // Returns number of samples read, or -1 on error/EOF.
    int readFrame(int16_t* buf, size_t samples);

    // Check if capture is active
    bool isOpen() const { return pipe_ != nullptr; }

private:
    FILE*        pipe_       = nullptr;
    unsigned int sample_rate_ = 16000;   // target: 16kHz mono S16_LE
    unsigned int channels_    = 1;
    unsigned int hw_rate_     = 48000;   // hardware native: 48kHz
    unsigned int hw_channels_ = 2;       // hardware native: stereo
    unsigned int hw_format_bits_ = 32;   // hardware native: S32_LE

    struct Biquad {
        float b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        float z1 = 0, z2 = 0;
        void setLowpass(float fs, float fc, float Q);
        float process(float x) {
            // Transposed Direct Form II
            float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
        void reset() { z1 = z2 = 0; }
    };
    Biquad lpf1_, lpf2_;

    // DC blocker（一阶高通，截止 ~12 Hz @ 16 kHz）
    float dc_x1_ = 0.0f;
    float dc_y1_ = 0.0f;
    static constexpr float DC_R = 0.995f;
};

} // namespace ft
