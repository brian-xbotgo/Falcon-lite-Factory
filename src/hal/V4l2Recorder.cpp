// V4l2Recorder — platform-independent V4L2 video capture.
// Uses standard Linux V4L2 API (videodev2.h).  No Rockchip or
// multi_media dependency.  Supports multiple cameras concurrently.

#include "hal/V4l2Recorder.h"
#include "hal/IRecorder.h"

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
    for (auto& wp : m_workers) {
        if (!startCamera(*wp)) {
            std::fprintf(stderr, "[v4l2] failed to start camera %s, aborting\n",
                         wp->cfg.device.c_str());
            // Stop any already-started cameras
            for (auto& wp2 : m_workers) {
                if (wp2->running) stopCamera(*wp2);
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
    for (auto& wp : m_workers) {
        stopCamera(*wp);
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

bool V4l2Recorder::startCamera(CameraWorker& w) {
    const auto& d = w.cfg.device;
    std::fprintf(stderr, "[v4l2] starting camera %s (%ux%u %s)\n",
                 d.c_str(), w.cfg.width, w.cfg.height, w.cfg.format.c_str());

    // Open output file
    if (!w.cfg.output.empty()) {
        w.outfile = std::fopen(w.cfg.output.c_str(), "wb");
        if (!w.outfile) {
            std::fprintf(stderr, "[v4l2] cannot create output file %s: %s\n",
                         w.cfg.output.c_str(), strerror(errno));
            return false;
        }
    } else {
        w.outfile = nullptr;
    }

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
            stopCamera(w);
            return false;
        }
    }

    // Open V4L2 device and set up streaming
    if (!v4l2Open(d, w))   { stopCamera(w); return false; }
    if (!v4l2SetFormat(w)) { stopCamera(w); return false; }
    v4l2SetFps(w.fd, w.cfg.fps);      // best-effort, ignore failure
    unsigned int type = bufType(w);
    if (!v4l2ReqBufs(w.fd, w.cfg.buffer_count, type))
                            { stopCamera(w); return false; }
    // STREAMON deferred to cameraLoop() — V4L2 spec requires QBUF before STREAMON

    // Start capture thread
    w.running = true;
    w.thread  = std::thread(&V4l2Recorder::cameraLoop, this, std::ref(w));

    return true;
}

void V4l2Recorder::stopCamera(CameraWorker& w) {
    w.running = false;

    if (w.thread.joinable()) {
        w.thread.join();
    }

    if (w.fd >= 0) {
        v4l2StreamOff(w.fd, bufType(w));
        ::close(w.fd);
        w.fd = -1;
    }

    if (w.outfile) {
        std::fclose(w.outfile);
        w.outfile = nullptr;
    }

    if (w.encoder.isInitialized()) {
        w.encoder.deinit();
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

        // Encode NV12 → H.264 and write to file
        size_t bytes_used = mplane ? planes[0].bytesused : buf.bytesused;
        if (bytes_used > 0 && w.outfile) {
            const uint8_t* enc_data = nullptr;
            size_t enc_size = 0;
            if (w.encoder.encode(static_cast<const uint8_t*>(bufs[buf.index].start),
                                 bytes_used, &enc_data, &enc_size)) {
                writeH264(w, enc_data, enc_size);
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
// Output helpers
// ──────────────────────────────────────────────────────────────────────────────

void V4l2Recorder::writeH264(CameraWorker& w, const uint8_t* data, size_t len) {
    if (!w.outfile || !data || len == 0) return;

    if (std::fwrite(data, 1, len, w.outfile) != len) {
        std::fprintf(stderr, "[v4l2] write H.264 failed for %s\n", w.cfg.device.c_str());
        w.running = false;
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// Factory
// ──────────────────────────────────────────────────────────────────────────────

V4l2Recorder* createV4l2Recorder(const RecorderConfig& cfg) {
    return new V4l2Recorder(cfg);
}

} // namespace ft
