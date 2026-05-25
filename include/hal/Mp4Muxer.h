#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <mutex>
#include <cstdio>

// Minimal MP4 muxer — writes moov-at-end MP4 files with H.264 video + G.711A audio.
// Thread-safe: addVideoFrame() and addAudioFrame() can be called from different threads.

namespace ft {

class Mp4Muxer {
public:
    Mp4Muxer() = default;
    ~Mp4Muxer();

    // Non-copyable (owns FILE handle)
    Mp4Muxer(const Mp4Muxer&) = delete;
    Mp4Muxer& operator=(const Mp4Muxer&) = delete;

    // Open output file. Returns false on error.
    bool open(const std::string& path);

    // Add a video frame (H.264 NAL unit, typically a complete AU).
    //   data: H.264 Annex B data
    //   len:  data length in bytes
    //   pts:  presentation timestamp in video timescale (90000 = 1 second)
    //   is_keyframe: true if this is an IDR frame
    // Returns false on write error.
    bool addVideoFrame(const uint8_t* data, size_t len, uint64_t pts, bool is_keyframe);

    // Add an audio frame (G.711 A-law encoded).
    //   data: A-law encoded audio data
    //   len:  data length in bytes (typically 320 for 20ms @ 16kHz mono)
    //   pts:  presentation timestamp in audio timescale (16000 = 1 second)
    // Returns false on write error.
    bool addAudioFrame(const uint8_t* data, size_t len, uint64_t pts);

    // Finalize and close the MP4 file. Writes moov box and patches mdat header.
    // Must be called after all frames have been added.
    // Returns false on error.
    bool finalize();

    // Close without finalizing (on error). Discards partial output.
    void close();

    bool isOpen() const { return file_ != nullptr; }

    // Track IDs and timescales
    static constexpr uint32_t VIDEO_TRACK_ID  = 1;
    static constexpr uint32_t AUDIO_TRACK_ID  = 2;
    static constexpr uint32_t VIDEO_TIMESCALE = 90000;
    static constexpr uint32_t AUDIO_TIMESCALE = 16000;

private:
    // Sample metadata recorded during writing
    struct SampleInfo {
        uint64_t pts;        // in track timescale
        uint32_t size;       // bytes
        uint32_t duration;   // in track timescale (for stts)
        bool     is_sync;    // keyframe (video only)
        uint64_t offset;     // absolute file offset where sample data was written
    };

    // Box writing helpers (write to internal buffer, then fwrite)
    void beginBox(uint32_t fourcc);
    void writeU32(uint32_t v);
    void writeU16(uint16_t v);
    void writeU64(uint64_t v);
    void writeU8(uint8_t v);
    void writeBytes(const uint8_t* data, size_t len);
    void writeFourCC(const char* s);
    void writeFixed16_16(double v);
    void writeFixed8_8(double v);
    void endBox();  // seek back to patch box size

    // Write accumulated buffer to file and reset
    bool flushBuffer();

    // Extract SPS/PPS from H.264 stream to build avcC box
    bool extractAvcC(const uint8_t* data, size_t len,
                     std::vector<uint8_t>& sps,
                     std::vector<uint8_t>& pps);

    // Box building
    void writeFtyp();
    void writeMdatHeader();
    void writeMoov();
    void writeMvhd();
    void writeVideoTrak();
    void writeAudioTrak();
    void writeTkhd(uint32_t trackId, uint32_t width, uint32_t height);
    void writeMdhd(uint32_t timescale, uint64_t duration);
    void writeHdlr(const char* handler_type, const char* name);
    void writeVmhd();
    void writeSmhd();
    void writeDref();
    void writeVideoStsd();
    void writeAudioStsd();
    void writeStts(const std::vector<SampleInfo>& samples, uint32_t timescale);
    void writeStsc();
    void writeStsz(const std::vector<SampleInfo>& samples);
    void writeStco(const std::vector<SampleInfo>& samples);
    void writeStss(const std::vector<SampleInfo>& samples);

    FILE*  file_ = nullptr;
    std::string path_;
    long   mdat_start_ = 0;       // file offset where mdat data begins
    long   mdat_header_pos_ = 0;  // file offset of mdat box header (to patch size)

    // Accumulated metadata
    std::vector<SampleInfo> video_samples_;
    std::vector<SampleInfo> audio_samples_;

    // Extracted SPS/PPS from first keyframe
    std::vector<uint8_t> sps_;
    std::vector<uint8_t> pps_;

    // Scratch buffer for box construction (avoids many small fwrites)
    std::vector<uint8_t> buf_;
    size_t               buf_start_ = 0;  // position in buf_ where current box started
    std::vector<size_t>  box_stack_;      // saves buf_start_ for nested boxes

    std::mutex mutex_;  // protects file writes from concurrent threads

    static constexpr size_t BUF_SIZE = 65536;
    static constexpr uint32_t VIDEO_WIDTH  = 1280;
    static constexpr uint32_t VIDEO_HEIGHT = 720;
};

} // namespace ft
