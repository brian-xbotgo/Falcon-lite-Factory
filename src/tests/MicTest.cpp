#include "tests/ITestModule.h"
#include "config/PlatformConfig.h"
#include "control/GpioController.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <sys/stat.h>
#include <unistd.h>

namespace ft {

namespace {

constexpr uint32_t kGainFail = 1u << 0;
constexpr uint32_t kRecordFail = 1u << 1;
constexpr uint32_t kBuzzerFail = 1u << 2;

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
            runCommand("chmod_wav", "chmod 644 " + shellQuote(recordPath));
            ::sync();
        }

        if (errorCode != 0) {
            TestResult result = TestResult::fail("mic record failed");
            result.data["error_code"] = errorCode;
            return result;
        }

        auto result = TestResult::pass();
        result.detail = "mic wav ready";
        result.data["record_path"] = recordPath;
        result.data["record_size"] = wavSize;
        return result;
    }
};

REGISTER_TEST_MODULE("mic", MicTest);

} // namespace ft
