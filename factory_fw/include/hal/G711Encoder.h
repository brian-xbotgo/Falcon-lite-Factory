#pragma once
#include <cstdint>
#include <cstddef>

// G.711 A-law encoder — 16-bit PCM to 8-bit A-law via standard lookup table.
// Used to compress audio for MP4 muxing with near-zero CPU overhead.

namespace ft {

class G711Encoder {
public:
    // Encode S16_LE PCM samples to A-law bytes.
    // pcm:      input, array of in_samples int16_t values
    // alaw:     output, must be at least in_samples bytes
    // in_samples: number of PCM samples to encode
    static void encode(const int16_t* pcm, uint8_t* alaw, size_t in_samples);

    // Audio frame constants (20ms frames)
    static constexpr int SAMPLE_RATE   = 16000;
    static constexpr int CHANNELS      = 1;
    static constexpr int FRAME_SAMPLES = 320;   // 20ms @ 16kHz
    static constexpr int FRAME_BYTES   = 320;   // A-law: 1 byte per sample
};

} // namespace ft
