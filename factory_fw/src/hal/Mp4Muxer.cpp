// Minimal MP4 muxer — moov-at-end layout with H.264 + G.711A tracks.
// Boxes are constructed in a scratch buffer and flushed to file.
// Sample data is written directly to the mdat region.

#include "hal/Mp4Muxer.h"
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <algorithm>

namespace ft {

// ──────────────────────────────────────────────────────────────────────────────
// Box write buffer helpers (big-endian)
// ──────────────────────────────────────────────────────────────────────────────

void Mp4Muxer::beginBox(uint32_t fourcc) {
    box_stack_.push_back(buf_start_);  // save parent box start for nesting
    buf_start_ = buf_.size();
    writeU32(0);          // placeholder size, patched by endBox()
    writeU32(fourcc);
}

void Mp4Muxer::writeU32(uint32_t v) {
    buf_.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
    buf_.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    buf_.push_back(static_cast<uint8_t>((v >> 8)  & 0xFF));
    buf_.push_back(static_cast<uint8_t>( v        & 0xFF));
}

void Mp4Muxer::writeU16(uint16_t v) {
    buf_.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    buf_.push_back(static_cast<uint8_t>( v       & 0xFF));
}

void Mp4Muxer::writeU64(uint64_t v) {
    writeU32(static_cast<uint32_t>((v >> 32) & 0xFFFFFFFF));
    writeU32(static_cast<uint32_t>( v        & 0xFFFFFFFF));
}

void Mp4Muxer::writeU8(uint8_t v) {
    buf_.push_back(v);
}

void Mp4Muxer::writeBytes(const uint8_t* data, size_t len) {
    buf_.insert(buf_.end(), data, data + len);
}

void Mp4Muxer::writeFourCC(const char* s) {
    writeU32(static_cast<uint32_t>(s[0]) << 24 |
             static_cast<uint32_t>(s[1]) << 16 |
             static_cast<uint32_t>(s[2]) << 8  |
             static_cast<uint32_t>(s[3]));
}

void Mp4Muxer::writeFixed16_16(double v) {
    writeU32(static_cast<uint32_t>(v * 65536.0));
}

void Mp4Muxer::writeFixed8_8(double v) {
    writeU16(static_cast<uint16_t>(v * 256.0));
}

void Mp4Muxer::endBox() {
    uint32_t size = static_cast<uint32_t>(buf_.size() - buf_start_);
    // Patch size at buf_start_
    buf_[buf_start_ + 0] = static_cast<uint8_t>((size >> 24) & 0xFF);
    buf_[buf_start_ + 1] = static_cast<uint8_t>((size >> 16) & 0xFF);
    buf_[buf_start_ + 2] = static_cast<uint8_t>((size >> 8)  & 0xFF);
    buf_[buf_start_ + 3] = static_cast<uint8_t>( size        & 0xFF);
    // Restore parent box start for correct nesting
    buf_start_ = box_stack_.back();
    box_stack_.pop_back();
}

bool Mp4Muxer::flushBuffer() {
    if (buf_.empty()) return true;
    size_t written = std::fwrite(buf_.data(), 1, buf_.size(), file_);
    if (written != buf_.size()) {
        std::fprintf(stderr, "[mp4] flushBuffer write error: %s\n", strerror(errno));
        return false;
    }
    buf_.clear();
    return true;
}

// ──────────────────────────────────────────────────────────────────────────────
// H.264 annex B → avcC extraction
// ──────────────────────────────────────────────────────────────────────────────

bool Mp4Muxer::extractAvcC(const uint8_t* data, size_t len,
                           std::vector<uint8_t>& sps,
                           std::vector<uint8_t>& pps) {
    // Scan for start codes (00 00 00 01 or 00 00 01) and extract NAL types 7 (SPS) and 8 (PPS)
    sps.clear();
    pps.clear();

    size_t i = 0;
    while (i + 4 <= len) {
        // Find start code
        size_t sc_len = 0;
        if (data[i] == 0x00 && data[i+1] == 0x00 &&
            data[i+2] == 0x00 && data[i+3] == 0x01) {
            sc_len = 4;
        } else if (data[i] == 0x00 && data[i+1] == 0x00 && data[i+2] == 0x01) {
            sc_len = 3;
        }

        if (sc_len > 0) {
            size_t nal_start = i + sc_len;
            // Find next start code or end of data
            size_t nal_end = len;
            for (size_t j = nal_start; j + 3 < len; ++j) {
                if ((data[j] == 0x00 && data[j+1] == 0x00 &&
                     (data[j+2] == 0x01 || (data[j+2] == 0x00 && data[j+3] == 0x01)))) {
                    nal_end = j;
                    break;
                }
            }

            if (nal_start < nal_end) {
                uint8_t nal_type = data[nal_start] & 0x1F;
                auto nal_data = std::vector<uint8_t>(data + nal_start, data + nal_end);
                if (nal_type == 7) {   // SPS
                    sps = std::move(nal_data);
                } else if (nal_type == 8) {  // PPS
                    pps = std::move(nal_data);
                }
            }

            if (!sps.empty() && !pps.empty()) return true;

            i = nal_end;
        } else {
            ++i;
        }
    }

    return !sps.empty() && !pps.empty();
}

// ──────────────────────────────────────────────────────────────────────────────
// Public API
// ──────────────────────────────────────────────────────────────────────────────

Mp4Muxer::~Mp4Muxer() {
    close();
}

bool Mp4Muxer::open(const std::string& path) {
    if (file_) {
        std::fprintf(stderr, "[mp4] already open\n");
        return false;
    }

    path_ = path;

    // Reset all state from previous recording session
    video_samples_.clear();
    audio_samples_.clear();
    sps_.clear();
    pps_.clear();
    buf_.clear();
    box_stack_.clear();
    buf_start_ = 0;

    file_ = std::fopen(path.c_str(), "wb");
    if (!file_) {
        std::fprintf(stderr, "[mp4] cannot create %s: %s\n", path.c_str(), strerror(errno));
        return false;
    }

    // Reserve scratch buffer
    buf_.reserve(BUF_SIZE);

    // Step 1: Write ftyp box
    writeFtyp();
    if (!flushBuffer()) { close(); return false; }

    // Step 2: Write placeholder mdat header (8 bytes: size + 'mdat')
    // We'll patch the size in finalize()
    mdat_header_pos_ = static_cast<long>(std::ftell(file_));
    uint8_t mdat_placeholder[8] = {0, 0, 0, 8, 'm', 'd', 'a', 't'};
    if (std::fwrite(mdat_placeholder, 1, 8, file_) != 8) {
        std::fprintf(stderr, "[mp4] write mdat header: %s\n", strerror(errno));
        close(); return false;
    }

    mdat_start_ = static_cast<long>(std::ftell(file_));
    std::fprintf(stderr, "[mp4] opened %s, mdat starts at offset %ld\n",
                 path.c_str(), mdat_start_);

    return true;
}

void Mp4Muxer::close() {
    if (file_) {
        std::fclose(file_);
        file_ = nullptr;
    }
    path_.clear();
    box_stack_.clear();
    video_samples_.clear();
    audio_samples_.clear();
    sps_.clear();
    pps_.clear();
    buf_.clear();
}

bool Mp4Muxer::addVideoFrame(const uint8_t* data, size_t len, uint64_t pts, bool is_keyframe) {
    if (!file_ || !data || len == 0) return false;

    // Extract SPS/PPS from first keyframe (Annex B → raw NAL for avcC)
    if (is_keyframe && sps_.empty()) {
        extractAvcC(data, len, sps_, pps_);
        if (!sps_.empty()) {
            std::fprintf(stderr, "[mp4] extracted SPS (%zu bytes) + PPS (%zu bytes)\n",
                         sps_.size(), pps_.size());
        }
    }

    // Convert H.264 from Annex B (start codes) to AVCC (4-byte length prefix).
    // MP4 requires AVCC format in mdat; players reject Annex B.
    std::vector<uint8_t> avcc;
    avcc.reserve(len);
    {
        size_t i = 0;
        while (i < len) {
            // Find start code: 00 00 01 or 00 00 00 01
            size_t sc_len = 0;
            if (i + 4 <= len && data[i] == 0x00 && data[i+1] == 0x00 &&
                data[i+2] == 0x00 && data[i+3] == 0x01) {
                sc_len = 4;
            } else if (i + 3 <= len && data[i] == 0x00 && data[i+1] == 0x00 &&
                       data[i+2] == 0x01) {
                sc_len = 3;
            } else {
                ++i;  // non-start-code byte, skip
                continue;
            }

            size_t nal_start = i + sc_len;
            if (nal_start >= len) break;

            // Find next start code (or end of data) to determine NAL end
            size_t nal_end = len;
            for (size_t j = nal_start; j + 2 < len; ++j) {
                if (data[j] == 0x00 && data[j+1] == 0x00) {
                    if (data[j+2] == 0x01) {
                        nal_end = j;
                        break;
                    }
                    if (j + 3 < len && data[j+2] == 0x00 && data[j+3] == 0x01) {
                        nal_end = j;
                        break;
                    }
                }
            }

            uint32_t nal_size = static_cast<uint32_t>(nal_end - nal_start);
            if (nal_size == 0) { i = nal_end; continue; }

            // Write 4-byte big-endian NAL length prefix
            avcc.push_back(static_cast<uint8_t>((nal_size >> 24) & 0xFF));
            avcc.push_back(static_cast<uint8_t>((nal_size >> 16) & 0xFF));
            avcc.push_back(static_cast<uint8_t>((nal_size >> 8)  & 0xFF));
            avcc.push_back(static_cast<uint8_t>( nal_size        & 0xFF));
            // Write NAL unit data
            avcc.insert(avcc.end(), data + nal_start, data + nal_end);

            i = nal_end;
        }
    }

    if (avcc.empty()) {
        std::fprintf(stderr, "[mp4] addVideoFrame: no NAL units found in annex B data\n");
        return false;
    }

    // Calculate duration from PTS delta (fallback: 3000 tick = 30fps @ 90kHz)
    uint32_t duration = VIDEO_TIMESCALE / 30;  // default 3000
    if (!video_samples_.empty()) {
        uint64_t prev_pts = video_samples_.back().pts;
        if (pts > prev_pts) {
            duration = static_cast<uint32_t>(pts - prev_pts);
        }
    }

    // Write AVCC-format sample data to mdat
    uint64_t file_offset = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        file_offset = static_cast<uint64_t>(std::ftell(file_));
        if (std::fwrite(avcc.data(), 1, avcc.size(), file_) != avcc.size()) {
            std::fprintf(stderr, "[mp4] write video frame: %s\n", strerror(errno));
            return false;
        }
    }

    SampleInfo info;
    info.pts      = pts;
    info.size     = static_cast<uint32_t>(avcc.size());  // AVCC size, not annex B
    info.duration = duration;
    info.is_sync  = is_keyframe;
    info.offset   = file_offset;
    video_samples_.push_back(info);

    return true;
}

bool Mp4Muxer::addAudioFrame(const uint8_t* data, size_t len, uint64_t pts) {
    if (!file_ || !data || len == 0) return false;

    // Duration: 320 ticks per frame (20ms @ 16kHz timescale)
    uint32_t duration = 320;

    uint64_t file_offset = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        file_offset = static_cast<uint64_t>(std::ftell(file_));
        if (std::fwrite(data, 1, len, file_) != len) {
            std::fprintf(stderr, "[mp4] write audio frame: %s\n", strerror(errno));
            return false;
        }
    }

    SampleInfo info;
    info.pts      = pts;
    info.size     = static_cast<uint32_t>(len);
    info.duration = duration;
    info.is_sync  = true;
    info.offset   = file_offset;
    audio_samples_.push_back(info);

    return true;
}

bool Mp4Muxer::finalize() {
    if (!file_) return false;

    std::fprintf(stderr, "[mp4] finalizing: %zu video + %zu audio samples\n",
                 video_samples_.size(), audio_samples_.size());

    // Step 1: Patch mdat box header with correct size
    long mdat_end = std::ftell(file_);
    uint32_t mdat_size = static_cast<uint32_t>(mdat_end - mdat_header_pos_);

    if (std::fseek(file_, mdat_header_pos_, SEEK_SET) != 0) {
        std::fprintf(stderr, "[mp4] seek to mdat header: %s\n", strerror(errno));
        return false;
    }
    writeU32(mdat_size);
    writeFourCC("mdat");
    if (!flushBuffer()) return false;

    // Return to end of file
    if (std::fseek(file_, 0, SEEK_END) != 0) {
        std::fprintf(stderr, "[mp4] seek to end: %s\n", strerror(errno));
        return false;
    }

    // Step 2: Write moov box at end of file
    writeMoov();
    if (!flushBuffer()) return false;

    std::fclose(file_);
    file_ = nullptr;
    std::fprintf(stderr, "[mp4] finalize complete\n");

    // ── Diagnostic: dump first 64 bytes + last 256 bytes ──
    {
        FILE* fd = std::fopen(path_.c_str(), "rb");
        if (fd) {
            std::fseek(fd, 0, SEEK_END);
            long total = std::ftell(fd);
            std::fprintf(stderr, "[mp4] file size: %ld bytes (%.1f KB)\n", total, total / 1024.0);

            // First 96 bytes: ftyp + mdat header + first ~64 bytes of mdat data
            std::fseek(fd, 0, SEEK_SET);
            uint8_t head[96];
            size_t n = std::fread(head, 1, sizeof(head), fd);
            std::fprintf(stderr, "[mp4] --- first %zu bytes ---\n", n);
            for (size_t i = 0; i < n; i += 16) {
                char line[128];
                int off = snprintf(line, sizeof(line), "  %04zx: ", i);
                for (size_t j = 0; j < 16 && i + j < n; ++j)
                    off += snprintf(line + off, sizeof(line) - off, "%02x ", head[i + j]);
                std::fprintf(stderr, "%s\n", line);
            }

            // Last 256 bytes: tail of mdat + moov
            long start = total > 256 ? total - 256 : 0;
            std::fseek(fd, start, SEEK_SET);
            uint8_t tail[256];
            n = std::fread(tail, 1, sizeof(tail), fd);
            std::fprintf(stderr, "[mp4] --- last %zu bytes (offset %ld) ---\n", n, start);
            for (size_t i = 0; i < n; i += 16) {
                char line[128];
                int off = snprintf(line, sizeof(line), "  %04lx: ", start + i);
                for (size_t j = 0; j < 16 && i + j < n; ++j)
                    off += snprintf(line + off, sizeof(line) - off, "%02x ", tail[i + j]);
                std::fprintf(stderr, "%s\n", line);
            }

            // Box scan: list all top-level boxes
            std::fprintf(stderr, "[mp4] --- box scan ---\n");
            long pos = 0;
            while (pos + 8 <= total) {
                std::fseek(fd, pos, SEEK_SET);
                uint8_t h[8];
                if (std::fread(h, 1, 8, fd) != 8) break;
                uint32_t bsize = ((uint32_t)h[0] << 24) | ((uint32_t)h[1] << 16) | ((uint32_t)h[2] << 8) | h[3];
                char fourcc[5] = {(char)h[4], (char)h[5], (char)h[6], (char)h[7], 0};
                std::fprintf(stderr, "  offset=%ld size=%u type='%s'\n", pos, bsize, fourcc);
                if (bsize == 0 || bsize > (uint32_t)(total - pos)) break;  // safety
                pos += bsize;
            }

            std::fclose(fd);
        }
    }

    return true;
}

// ──────────────────────────────────────────────────────────────────────────────
// Box construction — all write to buf_, caller flushes
// ──────────────────────────────────────────────────────────────────────────────

void Mp4Muxer::writeFtyp() {
    // ftyp: major_brand='isom', minor_version=512, compatible_brands=['isom','iso2','avc1','mp41']
    beginBox(0x66747970);  // 'ftyp'
    writeFourCC("isom");
    writeU32(512);
    writeFourCC("isom");
    writeFourCC("iso2");
    writeFourCC("avc1");
    writeFourCC("mp41");
    endBox();
}

void Mp4Muxer::writeMoov() {
    beginBox(0x6D6F6F76);  // 'moov'
    writeMvhd();

    if (!video_samples_.empty()) {
        writeVideoTrak();
    }
    if (!audio_samples_.empty()) {
        writeAudioTrak();
    }

    endBox();
}

void Mp4Muxer::writeMvhd() {
    beginBox(0x6D766864);  // 'mvhd'
    writeU32(0);                      // version/flags
    writeU32(0);                      // creation_time
    writeU32(0);                      // modification_time

    // timescale = video timescale (or 1000 if no video)
    uint32_t mvhd_timescale = video_samples_.empty() ? 1000 : VIDEO_TIMESCALE;
    writeU32(mvhd_timescale);

    // Duration: max of video and audio durations in mvhd timescale
    uint64_t dur_video = 0, dur_audio = 0;
    if (!video_samples_.empty()) {
        dur_video = video_samples_.back().pts + video_samples_.back().duration;
        dur_video = dur_video * mvhd_timescale / VIDEO_TIMESCALE;
    }
    if (!audio_samples_.empty()) {
        dur_audio = audio_samples_.back().pts + audio_samples_.back().duration;
        dur_audio = dur_audio * mvhd_timescale / AUDIO_TIMESCALE;
    }
    writeU32(static_cast<uint32_t>(std::max(dur_video, dur_audio)));

    writeU32(0x00010000);             // rate 1.0 (fixed 16.16)
    writeU16(0x0100);                 // volume 1.0 (fixed 8.8)
    writeU16(0);                      // reserved
    writeU32(0); writeU32(0);         // reserved
    // Matrix (unity)
    writeU32(0x00010000); writeU32(0); writeU32(0); writeU32(0);
    writeU32(0x00010000); writeU32(0); writeU32(0); writeU32(0);
    writeU32(0x40000000);
    // Pre-defined
    for (int i = 0; i < 6; ++i) writeU32(0);
    writeU32(2);  // next_track_id
    endBox();
}

void Mp4Muxer::writeVideoTrak() {
    beginBox(0x7472616B);  // 'trak'
    writeTkhd(VIDEO_TRACK_ID, VIDEO_WIDTH, VIDEO_HEIGHT);

    // mdia
    beginBox(0x6D646961);  // 'mdia'
    writeMdhd(VIDEO_TIMESCALE,
              video_samples_.empty() ? 0 :
              (video_samples_.back().pts + video_samples_.back().duration));
    writeHdlr("vide", "VideoHandler");

    // minf
    beginBox(0x6D696E66);  // 'minf'
    writeVmhd();
    writeDref();

    // stbl
    beginBox(0x7374626C);  // 'stbl'
    writeVideoStsd();
    writeStts(video_samples_, VIDEO_TIMESCALE);
    writeStsc();
    writeStsz(video_samples_);
    writeStco(video_samples_);
    writeStss(video_samples_);
    endBox();  // stbl

    endBox();  // minf
    endBox();  // mdia
    endBox();  // trak
}

void Mp4Muxer::writeAudioTrak() {
    beginBox(0x7472616B);  // 'trak'
    writeTkhd(AUDIO_TRACK_ID, 0, 0);

    // mdia
    beginBox(0x6D646961);  // 'mdia'
    writeMdhd(AUDIO_TIMESCALE,
              audio_samples_.empty() ? 0 :
              (audio_samples_.back().pts + audio_samples_.back().duration));
    writeHdlr("soun", "SoundHandler");

    // minf
    beginBox(0x6D696E66);  // 'minf'
    writeSmhd();
    writeDref();

    // stbl
    beginBox(0x7374626C);  // 'stbl'
    writeAudioStsd();
    writeStts(audio_samples_, AUDIO_TIMESCALE);
    writeStsc();
    writeStsz(audio_samples_);
    writeStco(audio_samples_);
    endBox();  // stbl

    endBox();  // minf
    endBox();  // mdia
    endBox();  // trak
}

void Mp4Muxer::writeTkhd(uint32_t trackId, uint32_t width, uint32_t height) {
    // version 0, flags = track_enabled | track_in_movie
    uint32_t flags = (trackId == VIDEO_TRACK_ID) ? 0x0003 : 0x0001;

    beginBox(0x746B6864);  // 'tkhd'
    writeU32(flags);                  // version(0) + flags
    writeU32(0);                      // creation_time
    writeU32(0);                      // modification_time
    writeU32(trackId);                // track_ID
    writeU32(0);                      // reserved
    writeU32(0);                      // duration (0 in tkhd, real duration in mdhd)
    writeU32(0); writeU32(0);         // reserved
    writeU16(0);                      // layer
    writeU16(0);                      // alternate_group
    writeFixed8_8(trackId == AUDIO_TRACK_ID ? 1.0 : 0.0);  // volume
    writeU16(0);                      // reserved
    // Matrix (unity)
    writeU32(0x00010000); writeU32(0); writeU32(0); writeU32(0);
    writeU32(0x00010000); writeU32(0); writeU32(0); writeU32(0);
    writeU32(0x40000000);
    // width / height (fixed 16.16)
    writeFixed16_16(static_cast<double>(width));
    writeFixed16_16(static_cast<double>(height));
    endBox();
}

void Mp4Muxer::writeMdhd(uint32_t timescale, uint64_t duration) {
    beginBox(0x6D646864);  // 'mdhd'
    writeU32(0);                      // version/flags
    writeU32(0);                      // creation_time
    writeU32(0);                      // modification_time
    writeU32(timescale);
    writeU32(static_cast<uint32_t>(duration));
    writeU16(0x55C4);                 // language = und (unspecified)
    writeU16(0);                      // pre_defined
    endBox();
}

void Mp4Muxer::writeHdlr(const char* handler_type, const char* name) {
    beginBox(0x68646C72);  // 'hdlr'
    writeU32(0);                      // version/flags
    writeU32(0);                      // pre_defined
    writeFourCC(handler_type);        // handler_type
    writeU32(0); writeU32(0); writeU32(0);  // reserved
    // name (null-terminated string)
    size_t name_len = std::strlen(name);
    for (size_t i = 0; i < name_len; ++i) writeU8(static_cast<uint8_t>(name[i]));
    writeU8(0);                       // null terminator
    endBox();
}

void Mp4Muxer::writeVmhd() {
    beginBox(0x766D6864);  // 'vmhd'
    writeU32(1);                      // version/flags
    writeU16(0);                      // graphicsmode
    writeU16(0); writeU16(0); writeU16(0);  // opcolor
    endBox();
}

void Mp4Muxer::writeSmhd() {
    beginBox(0x736D6864);  // 'smhd'
    writeU32(0);                      // version/flags
    writeFixed8_8(0.0);               // balance (0 = center)
    writeU16(0);                      // reserved
    endBox();
}

void Mp4Muxer::writeDref() {
    beginBox(0x64696E66);  // 'dinf'
    beginBox(0x64726566);  // 'dref'
    writeU32(0);                      // version/flags
    writeU32(1);                      // entry_count
    // url entry
    beginBox(0x75726C20);  // 'url '
    writeU32(1);                      // version/flags = self-contained
    endBox();  // url
    endBox();  // dref
    endBox();  // dinf
}

void Mp4Muxer::writeVideoStsd() {
    beginBox(0x73747364);  // 'stsd'
    writeU32(0);                      // version/flags
    writeU32(1);                      // entry_count

    // avc1 sample entry
    beginBox(0x61766331);  // 'avc1'
    writeU32(0); writeU16(0);         // reserved
    writeU16(1);                      // data_reference_index
    writeU16(0);                      // pre_defined
    writeU16(0);                      // reserved
    writeU32(0); writeU32(0); writeU32(0);  // pre_defined
    writeU16(VIDEO_WIDTH);
    writeU16(VIDEO_HEIGHT);
    writeU32(0x00480000);             // horizresolution 72 dpi
    writeU32(0x00480000);             // vertresolution 72 dpi
    writeU32(0);                      // reserved
    writeU16(1);                      // frame_count
    // compressor name (32 bytes, padded)
    char comp_name[32] = {};
    std::strncpy(comp_name, "AVC Coding", 31);
    for (int i = 0; i < 32; ++i) writeU8(static_cast<uint8_t>(comp_name[i]));
    writeU16(0x0018);                 // depth
    writeU16(0xFFFF);                 // pre_defined
    // avcC box
    beginBox(0x61766343);  // 'avcC'
    writeU8(1);                       // configurationVersion
    if (!sps_.empty()) {
        writeU8(sps_[1]);             // AVCProfileIndication
        writeU8(sps_[2]);             // profile_compatibility
        writeU8(sps_[3]);             // AVCLevelIndication
    } else {
        writeU8(0x42);                // Baseline Profile fallback
        writeU8(0x00);
        writeU8(0x1F);                // Level 3.1
    }
    writeU8(0xFF);                    // reserved(6)=111111 | lengthSizeMinusOne(2)=3 → 4-byte NAL length
    // SPS
    writeU8(0xE1);                    // numOfSequenceParameterSets | 0xE0
    writeU16(static_cast<uint16_t>(sps_.size()));
    if (!sps_.empty()) writeBytes(sps_.data(), sps_.size());
    // PPS
    writeU8(1);                       // numOfPictureParameterSets
    writeU16(static_cast<uint16_t>(pps_.size()));
    if (!pps_.empty()) writeBytes(pps_.data(), pps_.size());
    endBox();  // avcC

    endBox();  // avc1
    endBox();  // stsd
}

void Mp4Muxer::writeAudioStsd() {
    beginBox(0x73747364);  // 'stsd'
    writeU32(0);                      // version/flags
    writeU32(1);                      // entry_count

    // alaw sample entry
    beginBox(0x616C6177);  // 'alaw'
    writeU32(0); writeU16(0);         // reserved
    writeU16(1);                      // data_reference_index
    writeU16(1);                      // version (PCM)
    writeU16(0);                      // revision level
    writeU32(0);                      // vendor
    writeU16(1);                      // channel_count
    writeU16(8);                      // sample_size (bits)
    writeU16(0);                      // compression_id
    writeU16(0);                      // packet_size
    writeFixed16_16(static_cast<double>(AUDIO_TIMESCALE));  // sample_rate
    endBox();  // alaw

    endBox();  // stsd
}

void Mp4Muxer::writeStts(const std::vector<SampleInfo>& samples, uint32_t /*timescale*/) {
    beginBox(0x73747473);  // 'stts'
    writeU32(0);                      // version/flags

    if (samples.empty()) {
        writeU32(0);  // entry_count
        endBox();
        return;
    }

    // Compress consecutive samples with same duration
    struct SttsEntry { uint32_t count; uint32_t delta; };
    std::vector<SttsEntry> entries;
    uint32_t current_delta = samples[0].duration;
    uint32_t current_count = 1;

    for (size_t i = 1; i < samples.size(); ++i) {
        if (samples[i].duration == current_delta) {
            ++current_count;
        } else {
            entries.push_back({current_count, current_delta});
            current_delta = samples[i].duration;
            current_count = 1;
        }
    }
    entries.push_back({current_count, current_delta});

    writeU32(static_cast<uint32_t>(entries.size()));
    for (const auto& e : entries) {
        writeU32(e.count);
        writeU32(e.delta);
    }
    endBox();
}

void Mp4Muxer::writeStsc() {
    beginBox(0x73747363);  // 'stsc'
    writeU32(0);                      // version/flags
    writeU32(1);                      // entry_count
    writeU32(1);                      // first_chunk
    writeU32(1);                      // samples_per_chunk
    writeU32(1);                      // sample_description_index
    endBox();
}

void Mp4Muxer::writeStsz(const std::vector<SampleInfo>& samples) {
    beginBox(0x7374737A);  // 'stsz'
    writeU32(0);                      // version/flags
    writeU32(0);                      // sample_size (0 = variable)

    writeU32(static_cast<uint32_t>(samples.size()));
    for (const auto& s : samples) {
        writeU32(s.size);
    }
    endBox();
}

void Mp4Muxer::writeStco(const std::vector<SampleInfo>& samples) {
    beginBox(0x7374636F);  // 'stco'
    writeU32(0);                      // version/flags

    writeU32(static_cast<uint32_t>(samples.size()));
    for (const auto& s : samples) {
        writeU32(static_cast<uint32_t>(s.offset));
    }
    endBox();
}

void Mp4Muxer::writeStss(const std::vector<SampleInfo>& samples) {
    // Only write stss if there are sync samples
    uint32_t sync_count = 0;
    for (const auto& s : samples) {
        if (s.is_sync) ++sync_count;
    }

    beginBox(0x73747373);  // 'stss'
    writeU32(0);                      // version/flags
    writeU32(sync_count);

    for (size_t i = 0; i < samples.size(); ++i) {
        if (samples[i].is_sync) {
            writeU32(static_cast<uint32_t>(i + 1));  // 1-based index
        }
    }
    endBox();
}

} // namespace ft
