#ifndef AKIRA_IO_VIDEO_DECODER_HPP
#define AKIRA_IO_VIDEO_DECODER_HPP

#include <array>
#include <cstdint>
#include <cstddef>
#include <functional>
#include <memory>
#include <utility>
#include <span>
#include <vector>
#include <chrono>
#include <mutex>

#include "stream/hardware_decoder.hpp"
#include "util/av_wrappers.hpp"

class StreamDecoder
{
public:
    using Frame = akira::stream::HardwareDecoder::Frame;
    using FrameReadyCallback = std::function<void(Frame)>;

    StreamDecoder();
    ~StreamDecoder();

    // Disable copy
    StreamDecoder(const StreamDecoder&) = delete;
    StreamDecoder& operator=(const StreamDecoder&) = delete;

    void setFrameReadyCallback(FrameReadyCallback callback) { m_frame_ready_callback = std::move(callback); }

    bool initCodec(bool hevc, int width = 0, int height = 0);

    // Initialize video with frame queue
    bool initVideo(int video_width, int video_height);

    // Decode a video packet (returns false if waiting for keyframe)
    bool decode(const AVBufferHandle& data, std::size_t size);

    // Flush decoder buffers and set waiting-for-keyframe state
    void flush();

    // Cleanup
    void cleanup();

    int getVideoWidth() const { return m_video_width; }
    int getVideoHeight() const { return m_video_height; }
    void updateResolution(int width, int height) { m_video_width = width; m_video_height = height; }
    bool isHEVC() const { return m_is_hevc; }
    bool isHardwareAccelerated() const { return true; }

    struct DecoderStats
    {
        float decode_ms = 0.0f;
        float source_fps = 0.0f;
        float jitter_ms = 0.0f;
        uint64_t decoder_drops = 0;
    };

    DecoderStats getStats() const;
    std::chrono::steady_clock::time_point lastEmitTime() const { return m_last_emit; }
    bool lastDecodeCorrupt() const { return false; }

private:
    static constexpr std::uint8_t NalSlice = 1;
    static constexpr std::uint8_t NalIdr = 5;
    static constexpr std::uint8_t NalSps = 7;
    static constexpr std::uint8_t NalPps = 8;
    static constexpr std::array<std::uint8_t, 4> StartCode{0x00, 0x00, 0x00, 0x01};
    static constexpr std::uint32_t DecoderReferenceLimit = akira::stream::HardwareDecoder::ImageCount - 3;
    static constexpr std::chrono::milliseconds SubmitTimeout{50};
    static constexpr std::chrono::milliseconds FirstFrameWait{20};

    struct NalUnit
    {
        std::uint8_t type;
        std::span<const std::uint8_t> bytes;
    };

    struct Keyframe
    {
        bool idr = false;
        bool parameters_changed = false;
        bool has_sps = false;
        bool has_slices = false;
    };

    static std::vector<NalUnit> splitNalUnits(std::span<const std::uint8_t> sample);
    Keyframe scanNalUnits(std::span<const std::uint8_t> sample);
    std::vector<std::uint8_t> codecConfig() const;
    std::vector<std::uint8_t> decoderSample(std::span<const std::uint8_t> sample) const;
    void rememberDecoderSps();
    bool ensureHardwareDecoder(const Keyframe& keyframe);
    void emitReadyFrames(std::chrono::steady_clock::time_point decode_entry, std::chrono::nanoseconds first_wait);

    void noteDecodeSample(std::chrono::steady_clock::duration d);
    void notePacketArrival(std::chrono::steady_clock::time_point now);
    void publishStats(std::chrono::steady_clock::time_point now);

    mutable std::mutex m_stats_mutex;
    DecoderStats m_stats;
    uint64_t m_drops = 0;
    std::chrono::steady_clock::time_point m_last_emit{};
    std::chrono::steady_clock::time_point m_last_packet{};
    std::chrono::steady_clock::time_point m_window_start{};
    double m_decode_us_accum = 0.0;
    uint64_t m_decode_sample_count = 0;
    uint64_t m_frames_emitted = 0;
    uint64_t m_window_submitted = 0;
    uint64_t m_window_failed = 0;
    double m_interval_us_accum = 0.0;
    double m_interval_dev_accum = 0.0;
    uint64_t m_interval_count = 0;
    double m_interval_mean_us = 0.0;

    std::shared_ptr<akira::stream::HardwareDecoder> m_hardware;
    std::vector<std::uint8_t> m_sps;
    std::vector<std::uint8_t> m_decoder_sps;
    std::vector<std::uint8_t> m_pps;

    FrameReadyCallback m_frame_ready_callback;

    int m_video_width = 0;
    int m_video_height = 0;
    bool m_is_hevc = false;
    bool m_waiting_for_idr = false;
};

#endif // AKIRA_IO_VIDEO_DECODER_HPP
