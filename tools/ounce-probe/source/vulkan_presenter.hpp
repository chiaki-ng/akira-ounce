#pragma once

#include <switch.h>
#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "probe_log.hpp"
#include "stream/hardware_decoder.hpp"

class VulkanPresenter
{
public:
    static std::unique_ptr<VulkanPresenter> create(ProbeLog& log);

    ~VulkanPresenter();
    VulkanPresenter(const VulkanPresenter&) = delete;
    VulkanPresenter& operator=(const VulkanPresenter&) = delete;

    [[nodiscard]] bool present(const akira::stream::HardwareDecoder::Frame& frame);
    void probeImportOrders();

private:
    class SlotImport
    {
    public:
        SlotImport(VulkanPresenter& presenter, const akira::stream::HardwareFrame& frame);
        ~SlotImport();
        SlotImport(const SlotImport&) = delete;
        SlotImport& operator=(const SlotImport&) = delete;

        bool valid() const { return m_set != VK_NULL_HANDLE; }
        bool initialized() const { return m_initialized; }
        void markInitialized() { m_initialized = true; }
        VkImage luma() const { return m_luma; }
        VkImage chroma() const { return m_chroma; }
        VkDescriptorSet set() const { return m_set; }
        float uvScale() const { return m_uv_scale; }

    private:
        VkImage createPlane(VkFormat format, std::uint32_t texelSize, std::uint32_t width, std::uint32_t height,
            const akira::stream::HardwarePlane& plane);
        bool bindPlane(VkImage image, const akira::stream::HardwarePlane& plane, std::uint32_t texelSize, VkDeviceSize backingSize,
            const char* name);
        VkImageView createView(VkImage image, VkFormat format);

        VulkanPresenter& m_presenter;
        VkDeviceMemory m_memory = VK_NULL_HANDLE;
        VkImage m_luma = VK_NULL_HANDLE;
        VkImage m_chroma = VK_NULL_HANDLE;
        VkImageView m_luma_view = VK_NULL_HANDLE;
        VkImageView m_chroma_view = VK_NULL_HANDLE;
        VkDescriptorSet m_set = VK_NULL_HANDLE;
        bool m_initialized = false;
        float m_uv_scale = 1.0f;
    };

    struct InFlight
    {
        VkCommandBuffer commands = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        VkSemaphore acquired = VK_NULL_HANDLE;
        std::vector<akira::stream::HardwareDecoder::Frame> frames;
    };

    static constexpr std::uint32_t FramesInFlight = 2;
    static constexpr std::uint64_t DrmFormatModLinear = 0;

    explicit VulkanPresenter(ProbeLog& log);
    bool initialize();
    bool createInstance();
    bool createDevice();
    bool createSwapchain();
    bool createPipeline();
    bool createFrames();
    void reportImportSupport();
    bool linearModifierSupported(VkFormat format);
    bool linearHostImageSupported(VkFormat format);
    SlotImport* importFor(const akira::stream::HardwareFrame& frame);
    void recordFrame(InFlight& slot, std::uint32_t imageIndex, SlotImport& import, const akira::stream::HardwareFrame& frame);
    std::optional<std::uint32_t> memoryTypeFrom(std::uint32_t bits) const;
    std::optional<std::uint32_t> hostVisibleType() const;
    bool tryHostImport(const char* label, void* pointer, VkDeviceSize size, VkDeviceMemory* out);

    ProbeLog& m_log;
    VkInstance m_instance = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkPhysicalDevice m_physical = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_queue = VK_NULL_HANDLE;
    std::uint32_t m_queue_family = 0;
    std::set<std::string> m_extensions;
    bool m_host_import = false;
    bool m_drm_modifier = false;
    VkDeviceSize m_host_alignment = 0;
    PFN_vkGetMemoryHostPointerPropertiesEXT m_get_host_pointer_properties = nullptr;

    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    VkFormat m_format = VK_FORMAT_UNDEFINED;
    VkExtent2D m_extent{};
    std::vector<VkImage> m_images;
    std::vector<VkImageView> m_views;
    std::vector<VkSemaphore> m_rendered;

    VkSampler m_sampler = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_set_layout = VK_NULL_HANDLE;
    VkDescriptorPool m_pool = VK_NULL_HANDLE;
    VkPipelineLayout m_pipeline_layout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkCommandPool m_command_pool = VK_NULL_HANDLE;
    std::array<InFlight, FramesInFlight> m_in_flight{};
    std::uint32_t m_current = 0;

    std::uint64_t m_generation = 0;
    std::array<std::unique_ptr<SlotImport>, akira::stream::HardwareDecoder::ImageCount> m_imports{};
    std::vector<std::unique_ptr<SlotImport>> m_retired;
};
