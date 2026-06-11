#include "tests/ITestModule.h"
#include "config/PlatformConfig.h"
#include "control/GpioController.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <thread>
#include <sys/stat.h>
#include <unistd.h>

namespace ft {

namespace {

constexpr uint32_t kGainFail = 1u << 0;
constexpr uint32_t kRecordFail = 1u << 1;
constexpr uint32_t kBuzzerFail = 1u << 2;
constexpr uint32_t kAudioDetectedRmsThreshold = 420000;

struct AudioStats {
    uint32_t peak = 0;
    uint32_t rms = 0;
    uint16_t flags = 0;
    bool parsed = false;
};

uint16_t le16(const uint8_t* p)
{
    return static_cast<uint16_t>(p[0]) |
           static_cast<uint16_t>(p[1] << 8);
}

uint32_t le32(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

void appendLe16(std::string& out, uint16_t value)
{
    out.push_back(static_cast<char>(value & 0xff));
    out.push_back(static_cast<char>((value >> 8) & 0xff));
}

void appendLe32(std::string& out, uint32_t value)
{
    out.push_back(static_cast<char>(value & 0xff));
    out.push_back(static_cast<char>((value >> 8) & 0xff));
    out.push_back(static_cast<char>((value >> 16) & 0xff));
    out.push_back(static_cast<char>((value >> 24) & 0xff));
}

std::string packAudioStats(const AudioStats& stats)
{
    std::string out;
    out.reserve(10);
    appendLe32(out, stats.peak);
    appendLe32(out, stats.rms);
    appendLe16(out, stats.flags);
    return out;
}

std::string shellQuote(const std::string& value)
{
    std::string quoted = "'";
    for (char c : value) {
        if (c == '\'') {
            quoted += "'\\''";
        } else {
            quoted += c;
        }
    }
    quoted += "'";
    return quoted;
}

std::string dirnameOf(const std::string& path)
{
    const auto pos = path.find_last_of('/');
    if (pos == std::string::npos || pos == 0) {
        return pos == 0 ? std::string("/") : std::string(".");
    }
    return path.substr(0, pos);
}

bool fileHasPayload(const std::string& path)
{
    struct stat st {};
    return ::stat(path.c_str(), &st) == 0 && st.st_size > 44;
}

long long fileSize(const std::string& path)
{
    struct stat st {};
    if (::stat(path.c_str(), &st) != 0) {
        return -1;
    }
    return static_cast<long long>(st.st_size);
}

AudioStats analyzeWav(const std::string& path)
{
    AudioStats stats;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        std::fprintf(stderr, "[MicTest] wav_stats open failed path=%s\n", path.c_str());
        return stats;
    }

    uint8_t header[44] = {};
    if (std::fread(header, 1, sizeof(header), f) != sizeof(header)) {
        std::fprintf(stderr, "[MicTest] wav_stats short header path=%s\n", path.c_str());
        std::fclose(f);
        return stats;
    }

    if (std::memcmp(header + 0, "RIFF", 4) != 0 ||
        std::memcmp(header + 8, "WAVE", 4) != 0 ||
        std::memcmp(header + 12, "fmt ", 4) != 0 ||
        std::memcmp(header + 36, "data", 4) != 0) {
        std::fprintf(stderr, "[MicTest] wav_stats invalid signature path=%s\n", path.c_str());
        std::fclose(f);
        return stats;
    }

    const uint16_t channels = le16(header + 22);
    const uint16_t bitsPerSample = le16(header + 34);
    const uint32_t dataSize = le32(header + 40);
    const uint32_t bytesPerSample = bitsPerSample / 8;
    if (channels == 0 || bytesPerSample == 0 ||
        bytesPerSample > sizeof(uint32_t) || dataSize == 0 ||
        dataSize > 100u * 1024u * 1024u) {
        std::fprintf(stderr,
                     "[MicTest] wav_stats invalid layout path=%s channels=%u bits=%u data=%u\n",
                     path.c_str(), channels, bitsPerSample, dataSize);
        std::fclose(f);
        return stats;
    }

    const uint32_t frameBytes = bytesPerSample * channels;
    const uint32_t frames = dataSize / frameBytes;
    if (frameBytes == 0 || frames == 0) {
        std::fclose(f);
        return stats;
    }

    long double sumSq = 0;
    uint32_t samples = 0;
    for (uint32_t frame = 0; frame < frames; ++frame) {
        uint8_t sampleBuf[4] = {};
        if (std::fread(sampleBuf, 1, bytesPerSample, f) != bytesPerSample) {
            break;
        }

        int32_t sample = 0;
        if (bytesPerSample == 4) {
            sample = static_cast<int32_t>(le32(sampleBuf));
        } else if (bytesPerSample == 2) {
            sample = static_cast<int16_t>(le16(sampleBuf));
        } else if (bytesPerSample == 1) {
            sample = (static_cast<int32_t>(sampleBuf[0]) - 128) << 8;
        }

        const uint32_t absSample = sample < 0
            ? static_cast<uint32_t>(-(static_cast<int64_t>(sample)))
            : static_cast<uint32_t>(sample);
        if (absSample > stats.peak) {
            stats.peak = absSample;
        }
        sumSq += static_cast<long double>(absSample) * absSample;
        ++samples;

        if (channels > 1) {
            const long skip = static_cast<long>((channels - 1) * bytesPerSample);
            if (std::fseek(f, skip, SEEK_CUR) != 0) {
                break;
            }
        }
    }
    std::fclose(f);

    if (samples == 0) {
        return stats;
    }

    const long double rms = std::sqrt(sumSq / samples);
    stats.rms = rms > std::numeric_limits<uint32_t>::max()
        ? std::numeric_limits<uint32_t>::max()
        : static_cast<uint32_t>(rms);
    if (stats.rms > kAudioDetectedRmsThreshold) {
        stats.flags |= 0x0001;
    }
    stats.parsed = true;
    std::fprintf(stderr, "[MicTest] wav_stats path=%s peak=%u rms=%u flags=0x%04x parsed=1\n",
                 path.c_str(), stats.peak, stats.rms, stats.flags);
    return stats;
}

int runCommand(const std::string& tag, const std::string& cmd)
{
    std::fprintf(stderr, "[MicTest] %s cmd=%s\n", tag.c_str(), cmd.c_str());
    const int rc = std::system(cmd.c_str());
    std::fprintf(stderr, "[MicTest] %s rc=%d result=%s\n",
                 tag.c_str(), rc, rc == 0 ? "PASS" : "FAIL");
    return rc;
}

bool runBuzzerPattern(int buzzerGpio)
{
    bool ok = true;
    ok &= GpioController::exportGpio(buzzerGpio);
    ok &= GpioController::setDirection(buzzerGpio, "out");
    ok &= GpioController::write(buzzerGpio, 0);

    if (!ok) {
        std::fprintf(stderr, "[MicTest] buzzer init result=FAIL\n");
        return false;
    }

    for (int i = 0; i < 6; ++i) {
        const bool onOk = GpioController::write(buzzerGpio, 1);
        std::fprintf(stderr, "[MicTest] buzzer pulse=%d state=on result=%s\n",
                     i + 1, onOk ? "PASS" : "FAIL");
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        const bool offOk = GpioController::write(buzzerGpio, 0);
        std::fprintf(stderr, "[MicTest] buzzer pulse=%d state=off result=%s\n",
                     i + 1, offOk ? "PASS" : "FAIL");

        ok &= onOk && offOk;
        if (i == 1 || i == 3) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        } else if (i != 5) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    return ok;
}

} // namespace

class MicTest : public ITestModule {
public:
    TestResult run(TestContext& ctx) override {
        const auto& root = ctx.config().raw();
        const auto gpioCfg = root.value("gpio", nlohmann::json::object());
        const auto audioCfg = root.value("audio", nlohmann::json::object());

        const int buzzerGpio = gpioCfg.value("buzzer", 93);
        const std::string recordPath =
            audioCfg.value("record_path", std::string("/userdata/prod/test.wav"));
        const std::string recordDir = dirnameOf(recordPath);
        const std::string micDevice =
            audioCfg.value("mic_device", std::string("hw:0,0"));
        const std::string format =
            audioCfg.value("format", std::string("S32_LE"));
        const int sampleRate = audioCfg.value("sample_rate", 48000);
        const int channels = audioCfg.value("channels", 2);
        const int recordSeconds = audioCfg.value("record_seconds", 5);
        const std::string gainControl =
            audioCfg.value("pdm_gain_control", std::string("PDM1 Gain"));
        const int gainPercent = audioCfg.value("gain_percent", 100);

        std::fprintf(stderr,
                     "[MicTest] record_path=%s device=%s format=%s rate=%d channels=%d seconds=%d gain=%s:%d%% buzzer_gpio=%d\n",
                     recordPath.c_str(), micDevice.c_str(), format.c_str(),
                     sampleRate, channels, recordSeconds, gainControl.c_str(),
                     gainPercent, buzzerGpio);

        uint32_t errorCode = 0;

        if (runCommand("mkdir", "mkdir -p " + shellQuote(recordDir)) != 0) {
            errorCode |= kRecordFail;
        }
        runCommand("remove_old", "rm -f " + shellQuote(recordPath));

        const std::string gainCmd =
            "amixer sset " + shellQuote(gainControl) + " " +
            std::to_string(gainPercent) + "%";
        if (runCommand("set_gain", gainCmd) != 0) {
            errorCode |= kGainFail;
        }

        bool buzzerOk = true;
        std::thread buzzerThread([&]() {
            buzzerOk = runBuzzerPattern(buzzerGpio);
        });

        const std::string recordCmd =
            "arecord -D " + shellQuote(micDevice) +
            " -f " + shellQuote(format) +
            " -r " + std::to_string(sampleRate) +
            " -c " + std::to_string(channels) +
            " -t wav -V meter -d " + std::to_string(recordSeconds) +
            " " + shellQuote(recordPath);
        if (runCommand("record", recordCmd) != 0) {
            errorCode |= kRecordFail;
        }

        if (buzzerThread.joinable()) {
            buzzerThread.join();
        }

        if (!buzzerOk) {
            errorCode |= kBuzzerFail;
        }

        const bool wavReady = fileHasPayload(recordPath);
        const long long wavSize = fileSize(recordPath);
        std::fprintf(stderr, "[MicTest] wav path=%s ready=%d size=%lld buzzer=%s error_code=0x%08x\n",
                     recordPath.c_str(), wavReady ? 1 : 0, wavSize,
                     buzzerOk ? "PASS" : "FAIL", errorCode);
        if (!wavReady) {
            errorCode |= kRecordFail;
        } else {
            runCommand("chmod_record_dir", "chmod 755 " + shellQuote(recordDir));
            runCommand("chmod_wav", "chmod 644 " + shellQuote(recordPath));
            ::sync();
        }

        const AudioStats stats = wavReady ? analyzeWav(recordPath) : AudioStats{};
        const std::string audioStatsPayload = packAudioStats(stats);

        if (errorCode != 0) {
            TestResult result = TestResult::fail("mic record failed");
            result.responseExtra = audioStatsPayload;
            result.data["error_code"] = errorCode;
            result.data["audio_peak"] = stats.peak;
            result.data["audio_rms"] = stats.rms;
            result.data["audio_flags"] = stats.flags;
            return result;
        }

        auto result = TestResult::pass();
        result.detail = "mic wav ready";
        result.responseExtra = audioStatsPayload;
        result.data["record_path"] = recordPath;
        result.data["record_size"] = wavSize;
        result.data["audio_peak"] = stats.peak;
        result.data["audio_rms"] = stats.rms;
        result.data["audio_flags"] = stats.flags;
        return result;
    }
};

REGISTER_TEST_MODULE("mic", MicTest);

} // namespace ft
