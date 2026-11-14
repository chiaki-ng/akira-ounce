#ifndef AKIRA_STREAM_DECODER_OUTPUT_HPP
#define AKIRA_STREAM_DECODER_OUTPUT_HPP

#include <switch.h>
#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <mutex>
#include <string_view>
#include <vector>

namespace akira::stream
{

class DecoderOutput
{
public:
    struct Device
    {
        VkDevice device = VK_NULL_HANDLE;
        VkPhysicalDevice physical = VK_NULL_HANDLE;
    };

    struct Failure
    {
        std::string_view step;
        std::int64_t code;
    };

    static std::expected<std::unique_ptr<DecoderOutput>, Failure> acquire(const Device& device,
        const GsvDecodeImagePreference& preference);
    static void recycle(std::unique_ptr<DecoderOutput> output);

    ~DecoderOutput();
    DecoderOutput(const DecoderOutput&) = delete;
    DecoderOutput& operator=(const DecoderOutput&) = delete;

    const VideoDecoderImage& image() const { return m_image; }
    VkBuffer buffer() const { return m_buffer; }

private:
    static constexpr std::uint64_t PageSize = 0x1000;
    static constexpr std::uint8_t NativeMemoryType = 3;
    static constexpr std::uint32_t RegisterNativeMemory = 114;

    explicit DecoderOutput(const Device& device, const GsvDecodeImagePreference& preference);
    static std::vector<std::unique_ptr<DecoderOutput>>& spares();
    static std::mutex& sparesMutex();
    bool fits(const Device& device, const GsvDecodeImagePreference& preference) const;
    std::expected<void, Failure> build();
    std::expected<void, Failure> import();
    Result registerNative();

    Device m_device;
    GsvDecodeImagePreference m_preference;
    void* m_heap = nullptr;
    std::uint64_t m_size = 0;
    VkDeviceMemory m_memory = VK_NULL_HANDLE;
    VkBuffer m_buffer = VK_NULL_HANDLE;
    std::uint32_t m_memory_id = 0;
    VideoDecoderImage m_image{};
};

}

#endif
