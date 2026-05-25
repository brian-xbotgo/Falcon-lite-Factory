// V4l2Recorder — platform-independent V4L2 video capture.
// Uses standard Linux V4L2 API (videodev2.h).  No Rockchip or
// multi_media dependency.  Supports multiple cameras concurrently.
// Audio: captures from ALSA via arecord pipe, encodes to G.711A,
// and muxes with H.264 into an MP4 container.

#include "hal/V4l2Recorder.h"
#include "hal/IRecorder.h"
#include "hal/Mp4Muxer.h"
#include "hal/AudioCapture.h"
#include "hal/G711Encoder.h"

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <linux/videodev2.h>

#include <string>
#include <vector>

namespace ft {

// ──────────────────────────────────────────────────────────────────────────────
// Buffer type helper
// ──────────────────────────────────────────────────────────────────────────────

unsigned int V4l2Recorder::bufType(const CameraWorker& w) {
    return w.mplane ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE
                    : V4L2_BUF_TYPE_VIDEO_CAPTURE;
}

// ──────────────────────────────────────────────────────────────────────────────
// FOURCC helpers
// ──────────────────────────────────────────────────────────────────────────────

unsigned int V4l2Recorder::fourccFromString(const std::string& s) {
    if (s.size() < 4) return 0;
    return v4l2_fourcc(s[0], s[1], s[2], s[3]);
}

// ──────────────────────────────────────────────────────────────────────────────
// V4L2 low-level ops
// ──────────────────────────────────────────────────────────────────────────────

bool V4l2Recorder::v4l2Open(const std::string& device, CameraWorker& w) {
    int fd = ::open(device.c_str(), O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        std::fprintf(stderr, "[v4l2] cannot open %s: %s\n", device.c_str(), strerror(errno));
        return false;
    }

    v4l2_capability cap{};
    if (::ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
        std::fprintf(stderr, "[v4l2] VIDIOC_QUERYCAP %s: %s\n", device.c_str(), strerror(errno));
        ::close(fd);
        return false;
    }

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) &&
        !(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE)) {
        std::fprintf(stderr, "[v4l2] %s is not a video capture device\n", device.c_str());
        ::close(fd);
        return false;
    }

    if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
        std::fprintf(stderr, "[v4l2] %s does not support streaming I/O\n", device.c_str());
        ::close(fd);
        return false;
    }

    // Detect multi-plane: prefer device_caps for modern kernels
    unsigned int devcaps = (cap.capabilities & V4L2_CAP_DEVICE_CAPS)
                           ? cap.device_caps : cap.capabilities;
    w.mplane = (devcaps & V4L2_CAP_VIDEO_CAPTURE_MPLANE) != 0;

    std::fprintf(stderr, "[v4l2] opened %s: driver='%s' card='%s' bus='%s' %s\n",
                 device.c_str(), cap.driver, cap.card, cap.bus_info,
                 w.mplane ? "(mplane)" : "(single-plane)");
    w.fd = fd;
    return true;
}

bool V4l2Recorder::v4l2SetFormat(CameraWorker& w) {
    unsigned int w_pixfmt = fourccFromString(w.cfg.format);
    unsigned int type    = bufType(w);

    v4l2_format fmt{};
    fmt.type = type;

    if (w.mplane) {
        fmt.fmt.pix_mp.width       = w.cfg.width;
        fmt.fmt.pix_mp.height      = w.cfg.height;
        fmt.fmt.pix_mp.pixelformat = w_pixfmt;
        fmt.fmt.pix_mp.field       = V4L2_FIELD_NONE;
    } else {
        fmt.fmt.pix.width       = w.cfg.width;
        fmt.fmt.pix.height      = w.cfg.height;
        fmt.fmt.pix.pixelformat = w_pixfmt;
        fmt.fmt.pix.field       = V4L2_FIELD_NONE;
    }

    if (::ioctl(w.fd, VIDIOC_S_FMT, &fmt) < 0) {
        std::fprintf(stderr, "[v4l2] VIDIOC_S_FMT %ux%u %s: %s\n",
                     w.cfg.width, w.cfg.height, w.cfg.format.c_str(), strerror(errno));
        return false;
    }

    if (w.mplane) {
        std::fprintf(stderr, "[v4l2] format set: %ux%u %c%c%c%c (sizeimage=%u, mplane)\n",
                     fmt.fmt.pix_mp.width, fmt.fmt.pix_mp.height,
                     (char)(fmt.fmt.pix_mp.pixelformat & 0xFF),
                     (char)((fmt.fmt.pix_mp.pixelformat >> 8) & 0xFF),
                     (char)((fmt.fmt.pix_mp.pixelformat >> 16) & 0xFF),
                     (char)((fmt.fmt.pix_mp.pixelformat >> 24) & 0xFF),
                     fmt.fmt.pix_mp.plane_fmt[0].sizeimage);
    } else {
        std::fprintf(stderr, "[v4l2] format set: %ux%u %c%c%c%c (sizeimage=%u)\n",
                     fmt.fmt.pix.width, fmt.fmt.pix.height,
                     (char)(fmt.fmt.pix.pixelformat & 0xFF),
                     (char)((fmt.fmt.pix.pixelformat >> 8) & 0xFF),
                     (char)((fmt.fmt.pix.pixelformat >> 16) & 0xFF),
                     (char)((fmt.fmt.pix.pixelformat >> 24) & 0xFF),
                     fmt.fmt.pix.sizeimage);
    }
    return true;
}

bool V4l2Recorder::v4l2SetFps(int fd, unsigned int fps) {
    v4l2_streamparm parm{};
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parm.parm.capture.timeperframe.numerator   = 1;
    parm.parm.capture.timeperframe.denominator = fps;

    if (::ioctl(fd, VIDIOC_S_PARM, &parm) < 0) {
        std::fprintf(stderr, "[v4l2] VIDIOC_S_PARM fps=%u: %s (non-fatal)\n",
                     fps, strerror(errno));
        // non-fatal — some drivers don't support this
    }
    return true;
}

bool V4l2Recorder::v4l2ReqBufs(int fd, int count, unsigned int type) {
    v4l2_requestbuffers req{};
    req.type   = type;
    req.memory = V4L2_MEMORY_MMAP;
    req.count  = static_cast<unsigned int>(count);

    if (::ioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
        std::fprintf(stderr, "[v4l2] VIDIOC_REQBUFS count=%d: %s\n", count, strerror(errno));
        return false;
    }
    std::fprintf(stderr, "[v4l2] %u buffers allocated\n", req.count);
    return true;
}

bool V4l2Recorder::v4l2StreamOn(int fd, unsigned int type) {
    auto btype = static_cast<v4l2_buf_type>(type);
    if (::ioctl(fd, VIDIOC_STREAMON, &btype) < 0) {
        std::fprintf(stderr, "[v4l2] VIDIOC_STREAMON: %s\n", strerror(errno));
        return false;
    }
    return true;
}

bool V4l2Recorder::v4l2StreamOff(int fd, unsigned int type) {
    auto btype = static_cast<v4l2_buf_type>(type);
    if (::ioctl(fd, VIDIOC_STREAMOFF, &btype) < 0) {
        std::fprintf(stderr, "[v4l2] VIDIOC_STREAMOFF: %s\n", strerror(errno));
        return false;
    }
    return true;
}

// ──────────────────────────────────────────────────────────────────────────────
// Buffer management helpers
// ──────────────────────────────────────────────────────────────────────────────

struct MappedBuffer {
    void*  start = nullptr;
    size_t length = 0;
};

static std::vector<MappedBuffer> mapBuffers(int fd, int count, unsigned int type) {
    std::vector<MappedBuffer> bufs(count);
    bool mplane = (type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
    for (int i = 0; i < count; ++i) {
        v4l2_buffer buf{};
        v4l2_plane  planes[VIDEO_MAX_PLANES]{};
        buf.type   = type;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = static_cast<unsigned int>(i);
        if (mplane) {
            buf.m.planes = planes;
            buf.length   = VIDEO_MAX_PLANES;
        }

        if (::ioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) {
            std::fprintf(stderr, "[v4l2] VIDIOC_QUERYBUF %d: %s\n", i, strerror(errno));
            return {};
        }

        size_t length  = mplane ? planes[0].length : buf.length;
        off_t  offset  = mplane ? planes[0].m.mem_offset : buf.m.offset;

        bufs[i].start = ::mmap(nullptr, length,
                               PROT_READ | PROT_WRITE, MAP_SHARED, fd, offset);
        if (bufs[i].start == MAP_FAILED) {
            std::fprintf(stderr, "[v4l2] mmap %d: %s\n", i, strerror(errno));
            return {};
        }
        bufs[i].length = length;
    }
    return bufs;
}

static void unmapBuffers(std::vector<MappedBuffer>& bufs) {
    for (auto& b : bufs) {
        if (b.start && b.start != MAP_FAILED)
            ::munmap(b.start, b.length);
    }
    bufs.clear();
}

// ──────────────────────────────────────────────────────────────────────────────
// V4l2Recorder lifecycle
// ──────────────────────────────────────────────────────────────────────────────

V4l2Recorder::V4l2Recorder(const RecorderConfig& cfg)
    : m_cfg(cfg)
{
    m_workers.reserve(m_cfg.cameras.size());
    for (const auto& cam : m_cfg.cameras) {
        auto w = std::make_unique<CameraWorker>();
        w->cfg     = cam;
        w->running = false;
        m_workers.push_back(std::move(w));
    }
}

V4l2Recorder::~V4l2Recorder() {
    stop();
}

// ──────────────────────────────────────────────────────────────────────────────
// IRecorder interface
// ──────────────────────────────────────────────────────────────────────────────

bool V4l2Recorder::start(const RecorderCmd& /*cmd*/) {
    if (m_active) {
        std::fprintf(stderr, "[v4l2] already recording\n");
        return false;
    }
    if (!m_cfg.valid()) {
        std::fprintf(stderr, "[v4l2] no cameras configured\n");
        return false;
    }

    std::fprintf(stderr, "[v4l2] starting %zu camera(s)\n", m_workers.size());

    // Open all cameras first
    for (size_t i = 0; i < m_workers.size(); ++i) {
        bool isFirst = (i == 0);
        if (!startCamera(*m_workers[i], isFirst)) {
            std::fprintf(stderr, "[v4l2] failed to start camera %s, aborting\n",
                         m_workers[i]->cfg.device.c_str());
            // Stop any already-started cameras
            for (size_t j = 0; j < m_workers.size(); ++j) {
                if (m_workers[j]->running) stopCamera(*m_workers[j], j == 0);
            }
            return false;
        }
    }

    m_active = true;
    return true;
}

bool V4l2Recorder::stop() {
    if (!m_active) return false;

    std::fprintf(stderr, "[v4l2] stopping all cameras\n");

    // Signal all workers to stop
    for (auto& wp : m_workers) {
        wp->running = false;
    }

    // Join all threads
    for (size_t i = 0; i < m_workers.size(); ++i) {
        stopCamera(*m_workers[i], i == 0);
    }

    m_active = false;
    std::fprintf(stderr, "[v4l2] all cameras stopped\n");
    return true;
}

bool V4l2Recorder::isRecording() const {
    return m_active;
}

// ──────────────────────────────────────────────────────────────────────────────
// Per-camera control
// ──────────────────────────────────────────────────────────────────────────────

// Generate a unique output path under /userdata/record/ with auto-increment counter.
// Format: /userdata/record/cam{N}_{timestamp}_{count}.mp4
static std::string generateOutputPath(const std::string& base_name) {
    static int session_counter = 0;
    const std::string record_dir = "/userdata/record";

    // Create directory if not exists (best effort)
    struct stat st;
    if (::stat(record_dir.c_str(), &st) != 0) {
        if (::mkdir(record_dir.c_str(), 0755) != 0 && errno != EEXIST) {
            std::fprintf(stderr, "[v4l2] mkdir %s failed: %s\n", record_dir.c_str(), strerror(errno));
        }
    }

    // Extract camera identifier from base path (e.g. "cam0" from "/userdata/cam0_output.mp4")
    std::string cam_id = "cam";
    size_t slash = base_name.find_last_of('/');
    size_t start = (slash == std::string::npos) ? 0 : slash + 1;
    size_t under = base_name.find('_', start);
    if (under != std::string::npos && under > start) {
        cam_id = base_name.substr(start, under - start);
    }

    // Build path: /userdata/record/cam0_001.mp4
    char path[256];
    std::snprintf(path, sizeof(path), "%s/%s_%03d.mp4",
                  record_dir.c_str(), cam_id.c_str(), ++session_counter);
    return std::string(path);
}

bool V4l2Recorder::startCamera(CameraWorker& w, bool isFirst) {
    const auto& d = w.cfg.device;

    // Generate unique output path for this recording session
    std::string output_path = generateOutputPath(w.cfg.output);
    std::fprintf(stderr, "[v4l2] starting camera %s (%ux%u %s)%s -> %s\n",
                 d.c_str(), w.cfg.width, w.cfg.height, w.cfg.format.c_str(),
                 isFirst ? " [audio]" : "", output_path.c_str());

    // Open MP4 muxer (replaces raw fwrite)
    if (!output_path.empty()) {
        if (!w.muxer.open(output_path)) {
            std::fprintf(stderr, "[v4l2] cannot create output file %s\n", output_path.c_str());
            return false;
        }
    } else {
        std::fprintf(stderr, "[v4l2] no output path configured for %s\n", d.c_str());
        return false;
    }

    w.hasAudio = isFirst && m_cfg.audio.enabled;

    // Init MPP H.264 encoder
    {
        MppEncoderConfig enc_cfg;
        enc_cfg.width  = w.cfg.width;
        enc_cfg.height = w.cfg.height;
        enc_cfg.fps    = w.cfg.fps;
        enc_cfg.bitrate_kbps = 2000;
        enc_cfg.gop    = 60;
        if (!w.encoder.init(enc_cfg)) {
            std::fprintf(stderr, "[v4l2] failed to init MPP encoder for %s\n", d.c_str());
            stopCamera(w, isFirst);
            return false;
        }
    }

    // Open V4L2 device and set up streaming
    if (!v4l2Open(d, w))   { stopCamera(w, isFirst); return false; }
    if (!v4l2SetFormat(w)) { stopCamera(w, isFirst); return false; }
    v4l2SetFps(w.fd, w.cfg.fps);      // best-effort, ignore failure
    unsigned int type = bufType(w);
    if (!v4l2ReqBufs(w.fd, w.cfg.buffer_count, type))
                            { stopCamera(w, isFirst); return false; }
    // STREAMON deferred to cameraLoop() — V4L2 spec requires QBUF before STREAMON

    // Start capture thread
    w.running = true;
    w.thread  = std::thread(&V4l2Recorder::cameraLoop, this, std::ref(w));

    return true;
}

void V4l2Recorder::stopCamera(CameraWorker& w, bool isFirst) {
    w.running = false;

    if (w.thread.joinable()) {
        w.thread.join();
    }

    // Stop audio first (before closing muxer)
    // Audio thread writes to worker[0]->muxer, so it MUST be fully stopped
    // before finalizing the muxer, otherwise the MP4 file will be corrupted.
    if (isFirst && m_audioRunning.exchange(false)) {
        if (m_audioThread.joinable()) {
            m_audioThread.join();
        }
        m_audioCapture.close();
    }

    if (w.fd >= 0) {
        v4l2StreamOff(w.fd, bufType(w));
        ::close(w.fd);
        w.fd = -1;
    }

    if (w.encoder.isInitialized()) {
        w.encoder.deinit();
    }

    // Finalize MP4 muxer (writes moov box, closes file)
    // At this point audio thread is guaranteed to have exited, so no
    // concurrent writes to the file handle remain.
    if (w.muxer.isOpen()) {
        if (!w.muxer.finalize()) {
            std::fprintf(stderr, "[v4l2] muxer finalize failed for %s\n", w.cfg.device.c_str());
        }
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// Capture loop (runs in dedicated thread)
// ──────────────────────────────────────────────────────────────────────────────

void V4l2Recorder::cameraLoop(CameraWorker& w) {
    const std::string& device = w.cfg.device;
    unsigned int type = bufType(w);
    bool mplane = w.mplane;
    std::fprintf(stderr, "[v4l2] capture loop started for %s (%s)\n",
                 device.c_str(), mplane ? "mplane" : "single");

    // Map buffers in this thread's context
    auto bufs = mapBuffers(w.fd, w.cfg.buffer_count, type);
    if (bufs.empty()) {
        std::fprintf(stderr, "[v4l2] failed to map buffers for %s\n", device.c_str());
        w.running = false;
        return;
    }

    // Enqueue all buffers (must happen before STREAMON per V4L2 spec)
    for (int i = 0; i < w.cfg.buffer_count; ++i) {
        v4l2_buffer buf{};
        v4l2_plane  planes[VIDEO_MAX_PLANES]{};
        buf.type   = type;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = static_cast<unsigned int>(i);
        if (mplane) {
            buf.m.planes = planes;
            buf.length   = VIDEO_MAX_PLANES;
        }

        if (::ioctl(w.fd, VIDIOC_QBUF, &buf) < 0) {
            std::fprintf(stderr, "[v4l2] VIDIOC_QBUF %d: %s\n", i, strerror(errno));
            unmapBuffers(bufs);
            w.running = false;
            return;
        }
    }

    // Start streaming — now that all buffers are queued
    if (!v4l2StreamOn(w.fd, type)) {
        std::fprintf(stderr, "[v4l2] STREAMON failed for %s\n", device.c_str());
        unmapBuffers(bufs);
        w.running = false;
        return;
    }

    // Main capture loop
    int dropped  = 0;
    int captured = 0;

    while (w.running) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(w.fd, &fds);

        timeval tv{};
        tv.tv_sec  = 1;   // 1-second timeout → responsive to stop()
        tv.tv_usec = 0;

        int ret = ::select(w.fd + 1, &fds, nullptr, nullptr, &tv);
        if (ret < 0) {
            if (errno == EINTR) continue;
            std::fprintf(stderr, "[v4l2] select error on %s: %s\n",
                         device.c_str(), strerror(errno));
            break;
        }
        if (ret == 0) continue;  // timeout, check w.running again

        // Dequeue buffer
        v4l2_buffer buf{};
        v4l2_plane  planes[VIDEO_MAX_PLANES]{};
        buf.type   = type;
        buf.memory = V4L2_MEMORY_MMAP;
        if (mplane) {
            buf.m.planes = planes;
            buf.length   = VIDEO_MAX_PLANES;
        }

        if (::ioctl(w.fd, VIDIOC_DQBUF, &buf) < 0) {
            if (errno == EAGAIN) continue;
            std::fprintf(stderr, "[v4l2] VIDIOC_DQBUF error on %s: %s\n",
                         device.c_str(), strerror(errno));
            break;
        }

        if (buf.index >= static_cast<unsigned int>(w.cfg.buffer_count)) {
            std::fprintf(stderr, "[v4l2] invalid buffer index %u on %s\n",
                         buf.index, device.c_str());
            break;
        }

        // Encode NV12 → H.264 and feed to MP4 muxer
        size_t bytes_used = mplane ? planes[0].bytesused : buf.bytesused;
        if (bytes_used > 0 && w.muxer.isOpen()) {
            const uint8_t* enc_data = nullptr;
            size_t enc_size = 0;
            if (w.encoder.encode(static_cast<const uint8_t*>(bufs[buf.index].start),
                                 bytes_used, &enc_data, &enc_size)) {
                // Detect keyframe by checking NAL type 5 (IDR)
                bool is_key = false;
                if (enc_size >= 5) {
                    // Scan for start code + NAL header
                    for (size_t k = 0; k + 4 < enc_size; ++k) {
                        if (enc_data[k] == 0x00 && enc_data[k+1] == 0x00) {
                            uint8_t nal_type = 0;
                            if (enc_data[k+2] == 0x01) {
                                nal_type = enc_data[k+3] & 0x1F;
                            } else if (enc_data[k+2] == 0x00 && enc_data[k+3] == 0x01) {
                                if (k + 4 < enc_size) nal_type = enc_data[k+4] & 0x1F;
                            }
                            if (nal_type == 5) { is_key = true; break; }
                        }
                    }
                }

                // Start audio capture after first video frame is encoded
                // This ensures video data precedes audio data in the mdat box
                if (captured == 0 && w.hasAudio && !m_audioRunning.load()) {
                    if (m_audioCapture.open(m_cfg.audio.device,
                                            m_cfg.audio.sample_rate,
                                            m_cfg.audio.channels,
                                            m_cfg.audio.gain_db)) {
                        m_audioRunning = true;
                        m_audioThread = std::thread(&V4l2Recorder::audioLoop, this);
                        std::fprintf(stderr, "[v4l2] audio capture started after first video frame\n");
                    } else {
                        std::fprintf(stderr, "[v4l2] failed to start audio capture, continuing video-only\n");
                        w.hasAudio = false;
                    }
                }

                // PTS in 90kHz timescale from frame counter
                uint64_t video_pts = static_cast<uint64_t>(captured) * Mp4Muxer::VIDEO_TIMESCALE / w.cfg.fps;
                if (!w.muxer.addVideoFrame(enc_data, enc_size, video_pts, is_key)) {
                    std::fprintf(stderr, "[v4l2] muxer addVideoFrame failed on %s\n", device.c_str());
                }
            } else {
                std::fprintf(stderr, "[v4l2] encode failed on %s, frame dropped\n",
                             device.c_str());
            }
        }

        captured++;

        // Re-queue buffer
        if (::ioctl(w.fd, VIDIOC_QBUF, &buf) < 0) {
            std::fprintf(stderr, "[v4l2] VIDIOC_QBUF re-queue error on %s: %s\n",
                         device.c_str(), strerror(errno));
            dropped++;
        }
    }

    std::fprintf(stderr, "[v4l2] capture loop ended for %s: captured=%d dropped=%d\n",
                 device.c_str(), captured, dropped);

    unmapBuffers(bufs);
}

// ──────────────────────────────────────────────────────────────────────────────
// Audio capture loop (runs in dedicated thread when audio is enabled)
// ──────────────────────────────────────────────────────────────────────────────

void V4l2Recorder::audioLoop() {
    std::fprintf(stderr, "[audio] capture loop started\n");

    // PCM buffer: 20ms frame @ 16kHz mono = 320 samples
    int16_t pcm_buf[G711Encoder::FRAME_SAMPLES];
    uint8_t alaw_buf[G711Encoder::FRAME_BYTES];
    uint64_t audio_pts = 0;

    while (m_audioRunning) {
        int n = m_audioCapture.readFrame(pcm_buf, G711Encoder::FRAME_SAMPLES);
        if (n <= 0) {
            if (m_audioRunning) {
                std::fprintf(stderr, "[audio] read error, stopping\n");
                m_audioRunning = false;
            }
            break;
        }

        static FILE* dbg_pcm = std::fopen("/userdata/prod/debug_postfilter.pcm", "wb");
        if (dbg_pcm) {
            std::fwrite(pcm_buf, sizeof(int16_t),
                        G711Encoder::FRAME_SAMPLES, dbg_pcm);
            std::fflush(dbg_pcm);
        }
        // Encode PCM → G.711 A-law
        G711Encoder::encode(pcm_buf, alaw_buf, static_cast<size_t>(n));



        // Feed to the first camera's muxer (worker[0])
        if (!m_workers.empty() && m_workers[0]->muxer.isOpen()) {
            uint64_t pts = audio_pts;
            audio_pts += G711Encoder::FRAME_SAMPLES;  // 320 ticks per frame @ 16kHz
            if (!m_workers[0]->muxer.addAudioFrame(alaw_buf, G711Encoder::FRAME_BYTES, pts)) {
                std::fprintf(stderr, "[audio] muxer addAudioFrame failed\n");
            }
        }
    }

    std::fprintf(stderr, "[audio] capture loop ended\n");
}

// ──────────────────────────────────────────────────────────────────────────────
// Factory
// ──────────────────────────────────────────────────────────────────────────────

V4l2Recorder* createV4l2Recorder(const RecorderConfig& cfg) {
    return new V4l2Recorder(cfg);
}

} // namespace ft
