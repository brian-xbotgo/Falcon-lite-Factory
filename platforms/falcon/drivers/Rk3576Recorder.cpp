#include "drivers/Rk3576Recorder.h"
#include "drivers/RkMppEncoder.h"
#include "control/V4l2Recorder.h"

#include <cstdio>
#include <fstream>
#include <nlohmann/json.hpp>

namespace ft {

namespace {

CameraConfig cameraConfigFromJson(const nlohmann::json& j)
{
    CameraConfig cam;
    cam.device = j.value("device", cam.device);
    cam.width = j.value("width", cam.width);
    cam.height = j.value("height", cam.height);
    cam.format = j.value("format", cam.format);
    cam.fps = j.value("fps", cam.fps);
    cam.output = j.value("output", cam.output);
    cam.buffer_count = j.value("buffer_count", cam.buffer_count);
    return cam;
}

AudioConfig audioConfigFromJson(const nlohmann::json& j)
{
    AudioConfig audio;
    audio.enabled = j.value("enabled", audio.enabled);
    audio.device = j.value("device", audio.device);
    audio.sample_rate = j.value("sample_rate", audio.sample_rate);
    audio.channels = j.value("channels", audio.channels);
    audio.gain_db = j.value("gain_db", audio.gain_db);
    return audio;
}

RecorderConfig defaultRecorderConfig()
{
    RecorderConfig cfg;
    cfg.cameras.push_back({"/dev/video0", 1280, 720, "MJPG", 30, "/tmp/cam0.mp4", 4});
    cfg.audio.enabled = true;
    cfg.config_source = "fallback";
    return cfg;
}

RecorderConfig loadRecorderConfig(const std::string& path)
{
    std::ifstream in(path);
    if (!in.is_open()) {
        std::fprintf(stderr, "[Rk3576Recorder] config not found path=%s, using fallback\n",
                     path.c_str());
        return defaultRecorderConfig();
    }

    try {
        nlohmann::json root;
        in >> root;

        RecorderConfig cfg;
        cfg.config_source = path;
        cfg.max_duration_sec = root.value("max_duration_sec", cfg.max_duration_sec);

        const auto audio = root.value("audio", nlohmann::json::object());
        if (audio.is_object()) {
            cfg.audio = audioConfigFromJson(audio);
        }

        const auto cameras = root.value("cameras", nlohmann::json::array());
        if (cameras.is_array()) {
            for (const auto& item : cameras) {
                if (item.is_object()) {
                    cfg.cameras.push_back(cameraConfigFromJson(item));
                }
            }
        }

        if (!cfg.valid()) {
            std::fprintf(stderr, "[Rk3576Recorder] config has no cameras path=%s, using fallback\n",
                         path.c_str());
            return defaultRecorderConfig();
        }

        std::fprintf(stderr, "[Rk3576Recorder] loaded config path=%s cameras=%zu audio=%d\n",
                     path.c_str(), cfg.cameras.size(), cfg.audio.enabled ? 1 : 0);
        return cfg;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[Rk3576Recorder] config parse failed path=%s err=%s, using fallback\n",
                     path.c_str(), e.what());
        return defaultRecorderConfig();
    }
}

} // namespace

Rk3576Recorder::Rk3576Recorder()
{
    RecorderConfig cfg = loadRecorderConfig("/oem/usr/conf/recorder.json");
    recorder_.reset(createV4l2Recorder(
        cfg, [] { return std::make_unique<RkMppEncoder>(); }));
}

Rk3576Recorder::~Rk3576Recorder() = default;

bool Rk3576Recorder::start(const RecorderCmd& cmd)
{
    if (!recorder_) return false;
    return recorder_->start(cmd);
}

bool Rk3576Recorder::stop()
{
    if (!recorder_) return false;
    return recorder_->stop();
}

bool Rk3576Recorder::isRecording() const
{
    if (!recorder_) return false;
    return recorder_->isRecording();
}

} // namespace ft
