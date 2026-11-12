#include "stream/hardware_decoder.hpp"

#include <algorithm>
#include <atomic>

namespace akira::stream
{

HardwareFrame::HardwareFrame(std::shared_ptr<HardwareDecoder> owner, const VideoFrame& frame)
    : m_owner(std::move(owner)), m_frame(frame)
{
}

HardwareFrame::~HardwareFrame()
{
    m_owner->returnFrame(m_frame);
}

std::uint64_t HardwareFrame::generation() const
{
    return m_owner->generation();
}

const void* HardwareFrame::backing() const
{
    return m_owner->backing(m_frame.slot);
}

std::size_t HardwareFrame::backingSize() const
{
    return m_owner->backingSize(m_frame.slot);
}

HardwarePlane HardwareFrame::luma() const
{
    auto* base = static_cast<const std::uint8_t*>(backing());
    return {static_cast<std::size_t>(m_frame.planes[0] - base), m_frame.pitches[0]};
}

HardwarePlane HardwareFrame::chroma() const
{
    auto* base = static_cast<const std::uint8_t*>(backing());
    return {static_cast<std::size_t>(m_frame.planes[1] - base), m_frame.pitches[1]};
}

HardwareDecoder::HardwareDecoder(std::uint32_t width, std::uint32_t height)
    : m_width(width), m_height(height)
{
    static std::atomic<std::uint64_t> generations{0};
    m_generation = ++generations;
    m_returned.reserve(ImageCount);
}

std::expected<std::shared_ptr<HardwareDecoder>, Result> HardwareDecoder::create(const Config& config)
{
    std::shared_ptr<HardwareDecoder> decoder(new HardwareDecoder(config.width, config.height));
    const VideoDecoderConfig native{
        .codec = ViddecCodec_H264,
        .width = config.width,
        .height = config.height,
        .image_count = ImageCount,
        .frame_rate = 0,
        .tiling = ViddecTilingMode_PitchLinear,
        .color_policy = 0,
        .max_sample_size = 0,
        .codec_config = config.codecConfig.data(),
        .codec_config_size = config.codecConfig.size(),
    };
    if (const Result rc = videoDecoderCreate(&decoder->m_decoder, &native); R_FAILED(rc))
        return std::unexpected(rc);
    return decoder;
}

HardwareDecoder::~HardwareDecoder()
{
    videoDecoderClose(&m_decoder);
}

bool HardwareDecoder::timedOut(Result rc)
{
    return rc == KERNELRESULT(TimedOut);
}

std::expected<void, Result> HardwareDecoder::submit(std::span<const std::uint8_t> sample, std::int64_t timestampNs,
    std::chrono::nanoseconds timeout)
{
    constexpr auto slice = std::chrono::milliseconds(2);
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;)
    {
        releaseReturnedFrames();
        const auto remaining = std::max(std::chrono::nanoseconds(0),
            std::chrono::duration_cast<std::chrono::nanoseconds>(deadline - std::chrono::steady_clock::now()));
        const auto wait = std::min<std::chrono::nanoseconds>(remaining, slice);
        const Result rc = videoDecoderSubmit(&m_decoder, sample.data(), sample.size(), timestampNs, false,
            static_cast<u64>(wait.count()));
        if (R_SUCCEEDED(rc))
            return {};
        if (!timedOut(rc) || remaining <= wait)
            return std::unexpected(rc);
    }
}

std::optional<HardwareDecoder::Frame> HardwareDecoder::nextFrame(std::chrono::nanoseconds timeout)
{
    releaseReturnedFrames();
    VideoFrame frame{};
    if (R_FAILED(videoDecoderGetFrame(&m_decoder, &frame, static_cast<u64>(timeout.count()))))
        return std::nullopt;
    return std::make_shared<const HardwareFrame>(shared_from_this(), frame);
}

std::expected<void, Result> HardwareDecoder::flush()
{
    releaseReturnedFrames();
    if (const Result rc = videoDecoderFlush(&m_decoder); R_FAILED(rc))
        return std::unexpected(rc);
    return {};
}

const void* HardwareDecoder::backing(std::uint32_t slot) const
{
    return m_decoder.images[slot].backing;
}

std::size_t HardwareDecoder::backingSize(std::uint32_t slot) const
{
    return static_cast<std::size_t>(m_decoder.images[slot].backing_size);
}

void HardwareDecoder::returnFrame(const VideoFrame& frame)
{
    std::lock_guard lock(m_returned_mutex);
    m_returned.push_back(frame);
}

void HardwareDecoder::releaseReturnedFrames()
{
    std::vector<VideoFrame> returned;
    {
        std::lock_guard lock(m_returned_mutex);
        returned.swap(m_returned);
        m_returned.reserve(ImageCount);
    }
    std::ranges::for_each(returned, [this](VideoFrame& frame) { videoDecoderReleaseFrame(&m_decoder, &frame); });
}

}
