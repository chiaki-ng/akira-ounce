#ifndef AKIRA_STREAM_HARDWARE_DECODER_HPP
#define AKIRA_STREAM_HARDWARE_DECODER_HPP

#include <switch.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <vector>

namespace akira::stream
{

class HardwareDecoder;

struct HardwarePlane
{
    std::size_t offset;
    std::uint32_t pitch;
};

class HardwareFrame
{
public:
    HardwareFrame(std::shared_ptr<HardwareDecoder> owner, const VideoFrame& frame);
    ~HardwareFrame();
    HardwareFrame(const HardwareFrame&) = delete;
    HardwareFrame& operator=(const HardwareFrame&) = delete;

    std::uint32_t width() const { return m_frame.width; }
    std::uint32_t height() const { return m_frame.height; }
    std::uint32_t slot() const { return m_frame.slot; }
    std::int64_t timestampNs() const { return m_frame.timestamp_ns; }
    std::uint64_t generation() const;
    const void* backing() const;
    std::size_t backingSize() const;
    HardwarePlane luma() const;
    HardwarePlane chroma() const;

private:
    std::shared_ptr<HardwareDecoder> m_owner;
    VideoFrame m_frame;
};

class HardwareDecoder : public std::enable_shared_from_this<HardwareDecoder>
{
public:
    using Frame = std::shared_ptr<const HardwareFrame>;

    struct Config
    {
        std::uint32_t width;
        std::uint32_t height;
        std::span<const std::uint8_t> codecConfig;
    };

    static std::expected<std::shared_ptr<HardwareDecoder>, Result> create(const Config& config);

    ~HardwareDecoder();
    HardwareDecoder(const HardwareDecoder&) = delete;
    HardwareDecoder& operator=(const HardwareDecoder&) = delete;

    [[nodiscard]] std::expected<void, Result> submit(std::span<const std::uint8_t> sample, std::int64_t timestampNs,
        std::chrono::nanoseconds timeout);
    std::optional<Frame> nextFrame(std::chrono::nanoseconds timeout);
    [[nodiscard]] std::expected<void, Result> flush();

    std::uint64_t generation() const { return m_generation; }
    std::uint32_t width() const { return m_width; }
    std::uint32_t height() const { return m_height; }
    const void* backing(std::uint32_t slot) const;
    std::size_t backingSize(std::uint32_t slot) const;

    static constexpr std::uint32_t ImageCount = VIDDEC_MAX_IMAGES;

private:
    friend class HardwareFrame;

    HardwareDecoder(std::uint32_t width, std::uint32_t height);
    void returnFrame(const VideoFrame& frame);
    void releaseReturnedFrames();
    static bool timedOut(Result rc);

    ::VideoDecoder m_decoder{};
    std::uint32_t m_width;
    std::uint32_t m_height;
    std::uint64_t m_generation;
    std::mutex m_returned_mutex;
    std::vector<VideoFrame> m_returned;
};

}

#endif
