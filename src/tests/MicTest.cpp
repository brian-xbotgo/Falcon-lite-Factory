// Mic test: 11R / 23R
// Buzzer beep + arecord 5s → parse WAV for audio stats (peak / RMS)
// Response: 42-byte protocol + 10-byte audio_stats (peak4 + rms4 + flags2 LE)
// error_code: bit0=set gain failed, bit1=arecord failed

#include "tests/MicTest.h"
#include "core/TestEngine.h"
#include "common/Types.h"
#include "common/ShellUtils.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <thread>
#include <chrono>
#include <unistd.h>

namespace ft {

static constexpr const char* TEST_WAV = "/userdata/prod/test.wav";

// ---- WAV header helpers ----

struct WavHeader {
    uint32_t riff_id;
    uint32_t file_size;
    uint32_t wave_id;
    uint32_t fmt_id;
    uint32_t fmt_size;
    uint16_t audio_fmt;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
    uint32_t data_id;
    uint32_t data_size;
};

static_assert(sizeof(WavHeader) == 44, "WAV header must be 44 bytes");

struct AudioStats {
    uint32_t peak   = 0;    // max absolute sample value
    uint32_t rms    = 0;    // root-mean-square
    uint16_t flags  = 0;    // bit0 = audio detected (RMS > threshold)
    bool     parsed = false; // true if WAV was valid and stats were computed
};

static AudioStats analyze_wav(const char* path) {
    AudioStats s{};

    FILE* f = fopen(path, "rb");
    if (!f) {
        std::fprintf(stderr, "[mic] cannot open %s for analysis\n", path);
        return s;
    }

    WavHeader hdr;
    if (fread(&hdr, 1, sizeof(hdr), f) != sizeof(hdr)) {
        std::fprintf(stderr, "[mic] short WAV header\n");
        fclose(f);
        return s;
    }

    // Basic validation
    if (hdr.riff_id != 0x46464952 || hdr.wave_id != 0x45564157 ||
        hdr.fmt_id  != 0x20746d66 || hdr.data_id != 0x61746164) {
        std::fprintf(stderr, "[mic] invalid WAV signature\n");
        fclose(f);
        return s;
    }

    uint32_t data_len = hdr.data_size;
    if (data_len == 0 || data_len > 100 * 1024 * 1024) {  // sanity: max 100MB
        std::fprintf(stderr, "[mic] bad data size: %u\n", data_len);
        fclose(f);
        return s;
    }

    uint16_t bits  = hdr.bits_per_sample;
    uint16_t chans = hdr.num_channels;
    uint32_t sample_count = data_len / (bits / 8 * chans);

    if (sample_count == 0 || bits == 0 || chans == 0) {
        fclose(f);
        return s;
    }

    // Read PCM samples, compute peak + sum-of-squares
    int64_t sum_sq = 0;
    int32_t peak = 0;

    if (bits == 32 && chans == 2) {
        // S32_LE stereo — fast path
        for (uint32_t i = 0; i < sample_count; ++i) {
            int32_t left, right;
            if (fread(&left, 4, 1, f) != 1) break;
            if (fread(&right, 4, 1, f) != 1) break;

            int32_t abs_l = (left < 0) ? -left : left;
            if (abs_l > peak) peak = abs_l;
            sum_sq += int64_t(left) * left;
        }
    } else {
        // Generic path: read frames
        int bps = bits / 8;
        uint8_t* buf = new uint8_t[data_len];
        fseek(f, sizeof(hdr), SEEK_SET);
        size_t rd = fread(buf, 1, data_len, f);

        for (size_t i = 0; i + bps <= rd; i += bps * chans) {
            // Just use the first channel
            int32_t val = 0;
            if (bps == 4) {
                memcpy(&val, buf + i, 4);
            } else if (bps == 2) {
                int16_t v;
                memcpy(&v, buf + i, 2);
                val = v;
            } else if (bps == 1) {
                val = int32_t(buf[i]) << 8;
            }
            if (val < 0) val = -val;
            if (val > peak) peak = val;
            sum_sq += int64_t(val) * val;
        }
        delete[] buf;
    }

    fclose(f);

    s.peak = static_cast<uint32_t>(peak);
    uint64_t mean_sq = static_cast<uint64_t>(sum_sq) / sample_count;
    // Integer sqrt approximation
    uint32_t rms = 0;
    {
        uint64_t x = mean_sq;
        uint64_t lo = 0, hi = (x >> 1) + 1;
        while (lo < hi) {
            uint64_t mid = (lo + hi + 1) >> 1;
            if (mid * mid <= x) lo = mid; else hi = mid - 1;
        }
        rms = static_cast<uint32_t>(lo);
    }
    s.rms = rms;
    s.parsed = true;

    // "Audio detected" if RMS > ~5% of 24-bit range (≈ 420k for S32_LE)
    if (s.rms > 420000)
        s.flags |= 0x0001;

    std::fprintf(stderr, "[mic] audio stats: peak=%u, rms=%u, detected=%d\n",
                 s.peak, s.rms, (s.flags & 1));

    return s;
}

// ---- Buzzer ----

static void buzzer_play() {
    auto gpio_set = [](int val) {
        char cmd[64];
        std::snprintf(cmd, sizeof(cmd), "echo %d > /sys/class/gpio/gpio93/value", val);
        std::system(cmd);
    };

    for (int group = 0; group < 3; ++group) {
        gpio_set(1); usleep(200 * 1000);
        gpio_set(0); usleep(100 * 1000);
        gpio_set(1); usleep(200 * 1000);
        gpio_set(0);

        if (group < 2) sleep(1);
    }
}

// ---- Core test ----

static void do_mic_test_raw(const ProtoHeader& hdr, TestEngine* engine,
                            const std::string& resp_topic) {
    uint32_t err = 0;
    AudioStats stats;

    std::system("rm -f /userdata/prod/test.wav");
    std::system("mkdir -p /userdata/prod");

    std::system("echo 93 > /sys/class/gpio/export 2>/dev/null");
    std::system("echo out > /sys/class/gpio/gpio93/direction");

    // Enable PDM0 mic gain (card 1 = rockchip,pdm-mic-array)
    if (!shell_ok("amixer -c 1 sset 'PDM0 Gain Volume 0' 100%")) {
        err |= 0x0001;
        std::fprintf(stderr, "[mic] set gain failed\n");
        goto respond;
    }

    {
        std::thread buzzer_thread(buzzer_play);

        auto r = shell_exec(
            "arecord -D hw:1,0 -f S32_LE -r 48000 -c 2 -t wav -V meter -d 5 "
            "/userdata/prod/test.wav"
        );
        if (r.exit_code != 0) {
            err |= 0x0002;
            std::fprintf(stderr, "[mic] arecord failed: %s\n", r.output.c_str());
        }

        if (buzzer_thread.joinable()) buzzer_thread.join();
    }

    // Analyze the recorded WAV
    stats = analyze_wav(TEST_WAV);

    std::fprintf(stderr, "[mic] test done, sn=%s, err=0x%08X\n", hdr.sn, err);

respond:
    // Build response: 42-byte protocol + 10-byte audio stats
    std::string proto_resp = hdr.build_response(err);  // 42 bytes
    std::string resp = proto_resp;

    // Append audio stats (10 bytes, little-endian)
    resp.append(1, static_cast<char>(stats.peak & 0xFF));
    resp.append(1, static_cast<char>((stats.peak >> 8) & 0xFF));
    resp.append(1, static_cast<char>((stats.peak >> 16) & 0xFF));
    resp.append(1, static_cast<char>((stats.peak >> 24) & 0xFF));

    resp.append(1, static_cast<char>(stats.rms & 0xFF));
    resp.append(1, static_cast<char>((stats.rms >> 8) & 0xFF));
    resp.append(1, static_cast<char>((stats.rms >> 16) & 0xFF));
    resp.append(1, static_cast<char>((stats.rms >> 24) & 0xFF));

    resp.append(1, static_cast<char>(stats.flags & 0xFF));
    resp.append(1, static_cast<char>((stats.flags >> 8) & 0xFF));

    if (engine)
        engine->publish(resp_topic, resp, 2);
}

void register_mic_tests(TestEngine& engine) {
    engine.registerRaw("11R", [&engine](const uint8_t* data, size_t len) {
        ProtoHeader hdr;
        if (!hdr.parse(data, len)) return;
        save_sn(hdr.sn);
        do_mic_test_raw(hdr, &engine, "11A");
    }, /*async=*/true);

    engine.registerRaw("23R", [&engine](const uint8_t* data, size_t len) {
        ProtoHeader hdr;
        if (!hdr.parse(data, len)) return;
        save_sn(hdr.sn);
        do_mic_test_raw(hdr, &engine, "23A");
    }, /*async=*/true);
}

} // namespace ft
