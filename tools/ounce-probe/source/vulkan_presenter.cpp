#include "vulkan_presenter.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <ranges>

#include "fullscreen_vert.h"
#include "nv12_frag.h"

std::unique_ptr<VulkanPresenter> VulkanPresenter::create(ProbeLog& log)
{
    std::unique_ptr<VulkanPresenter> presenter(new VulkanPresenter(log));
    if (!presenter->initialize())
        return {};
    return presenter;
}

VulkanPresenter::VulkanPresenter(ProbeLog& log) : m_log(log)
{
}

bool VulkanPresenter::initialize()
{
    return createInstance() && createDevice() && createSwapchain() && createPipeline() && createFrames();
}

VulkanPresenter::~VulkanPresenter()
{
    if (m_device)
        vkDeviceWaitIdle(m_device);
    for (InFlight& slot : m_in_flight)
        slot.frames.clear();
    std::ranges::for_each(m_imports, [](auto& import) { import.reset(); });
    m_retired.clear();
    if (m_device)
    {
        for (InFlight& slot : m_in_flight)
        {
            vkDestroyFence(m_device, slot.fence, nullptr);
            vkDestroySemaphore(m_device, slot.acquired, nullptr);
        }
        std::ranges::for_each(m_rendered, [this](VkSemaphore s) { vkDestroySemaphore(m_device, s, nullptr); });
        vkDestroyCommandPool(m_device, m_command_pool, nullptr);
        vkDestroyPipeline(m_device, m_pipeline, nullptr);
        vkDestroyPipelineLayout(m_device, m_pipeline_layout, nullptr);
        vkDestroyDescriptorPool(m_device, m_pool, nullptr);
        vkDestroyDescriptorSetLayout(m_device, m_set_layout, nullptr);
        vkDestroySampler(m_device, m_sampler, nullptr);
        std::ranges::for_each(m_views, [this](VkImageView v) { vkDestroyImageView(m_device, v, nullptr); });
        vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
        vkDestroyDevice(m_device, nullptr);
    }
    if (m_instance)
    {
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
        vkDestroyInstance(m_instance, nullptr);
    }
}

bool VulkanPresenter::createInstance()
{
    const VkApplicationInfo app{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "ounce-probe",
        .apiVersion = VK_API_VERSION_1_3,
    };
    const std::array extensions{VK_KHR_SURFACE_EXTENSION_NAME, VK_NN_VI_SURFACE_EXTENSION_NAME};
    const VkInstanceCreateInfo info{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app,
        .enabledExtensionCount = static_cast<std::uint32_t>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data(),
    };
    if (const VkResult r = vkCreateInstance(&info, nullptr, &m_instance); r != VK_SUCCESS)
    {
        m_log.line("FAIL vkCreateInstance {}", static_cast<int>(r));
        return false;
    }
    const VkViSurfaceCreateInfoNN surface{
        .sType = VK_STRUCTURE_TYPE_VI_SURFACE_CREATE_INFO_NN,
        .window = nwindowGetDefault(),
    };
    if (const VkResult r = vkCreateViSurfaceNN(m_instance, &surface, nullptr, &m_surface); r != VK_SUCCESS)
    {
        m_log.line("FAIL vkCreateViSurfaceNN {}", static_cast<int>(r));
        return false;
    }
    return true;
}

bool VulkanPresenter::createDevice()
{
    std::uint32_t count = 1;
    vkEnumeratePhysicalDevices(m_instance, &count, &m_physical);
    if (count == 0)
    {
        m_log.line("FAIL no physical device");
        return false;
    }

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(m_physical, &properties);
    m_log.line("device {} api {}.{}.{} driver 0x{:x}", properties.deviceName, VK_API_VERSION_MAJOR(properties.apiVersion),
        VK_API_VERSION_MINOR(properties.apiVersion), VK_API_VERSION_PATCH(properties.apiVersion), properties.driverVersion);

    std::uint32_t extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(m_physical, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> available(extensionCount);
    vkEnumerateDeviceExtensionProperties(m_physical, nullptr, &extensionCount, available.data());
    for (const VkExtensionProperties& e : available)
        m_extensions.insert(e.extensionName);
    m_host_import = m_extensions.contains(VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME);
    m_drm_modifier = m_extensions.contains(VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME);
    m_log.line("{} device extensions; external_memory_host={} image_drm_format_modifier={} push_descriptor={}",
        m_extensions.size(), m_host_import, m_drm_modifier, m_extensions.contains(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME));

    std::uint32_t families = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(m_physical, &families, nullptr);
    std::vector<VkQueueFamilyProperties> familyProperties(families);
    vkGetPhysicalDeviceQueueFamilyProperties(m_physical, &families, familyProperties.data());
    const auto family = std::ranges::find_if(std::views::iota(0u, families), [&](std::uint32_t i) {
        VkBool32 present = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(m_physical, i, m_surface, &present);
        return (familyProperties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present;
    });
    if (family == std::views::iota(0u, families).end())
    {
        m_log.line("FAIL no graphics queue that presents");
        return false;
    }
    m_queue_family = *family;

    std::vector<const char*> enabled{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    if (m_host_import)
        enabled.push_back(VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME);
    if (m_drm_modifier)
        enabled.push_back(VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME);

    VkPhysicalDeviceVulkan13Features features13{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .synchronization2 = VK_TRUE,
        .dynamicRendering = VK_TRUE,
    };
    const float priority = 1.0f;
    const VkDeviceQueueCreateInfo queue{
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = m_queue_family,
        .queueCount = 1,
        .pQueuePriorities = &priority,
    };
    const VkDeviceCreateInfo info{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &features13,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue,
        .enabledExtensionCount = static_cast<std::uint32_t>(enabled.size()),
        .ppEnabledExtensionNames = enabled.data(),
    };
    if (const VkResult r = vkCreateDevice(m_physical, &info, nullptr, &m_device); r != VK_SUCCESS)
    {
        m_log.line("FAIL vkCreateDevice {}", static_cast<int>(r));
        return false;
    }
    vkGetDeviceQueue(m_device, m_queue_family, 0, &m_queue);
    reportImportSupport();
    return true;
}

void VulkanPresenter::reportImportSupport()
{
    if (m_host_import)
    {
        VkPhysicalDeviceExternalMemoryHostPropertiesEXT host{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_HOST_PROPERTIES_EXT};
        VkPhysicalDeviceProperties2 properties{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &host};
        vkGetPhysicalDeviceProperties2(m_physical, &properties);
        m_host_alignment = host.minImportedHostPointerAlignment;
        m_get_host_pointer_properties = reinterpret_cast<PFN_vkGetMemoryHostPointerPropertiesEXT>(
            vkGetDeviceProcAddr(m_device, "vkGetMemoryHostPointerPropertiesEXT"));
        m_log.line("minImportedHostPointerAlignment {} vkGetMemoryHostPointerPropertiesEXT {}", m_host_alignment,
            m_get_host_pointer_properties != nullptr);
    }
    if (m_drm_modifier)
        m_log.line("linear modifier sampled+host import: R8 {} R8G8 {}", linearModifierSupported(VK_FORMAT_R8_UNORM),
            linearModifierSupported(VK_FORMAT_R8G8_UNORM));
    m_log.line("linear tiling sampled+host import: R8 {} R8G8 {}", linearHostImageSupported(VK_FORMAT_R8_UNORM),
        linearHostImageSupported(VK_FORMAT_R8G8_UNORM));
}

bool VulkanPresenter::linearHostImageSupported(VkFormat format)
{
    VkFormatProperties formatProperties{};
    vkGetPhysicalDeviceFormatProperties(m_physical, format, &formatProperties);
    if (!(formatProperties.linearTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT))
        return false;
    const VkPhysicalDeviceExternalImageFormatInfo external{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO,
        .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT,
    };
    const VkPhysicalDeviceImageFormatInfo2 info{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
        .pNext = &external,
        .format = format,
        .type = VK_IMAGE_TYPE_2D,
        .tiling = VK_IMAGE_TILING_LINEAR,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT,
    };
    VkExternalImageFormatProperties externalProperties{.sType = VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES};
    VkImageFormatProperties2 properties{.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2, .pNext = &externalProperties};
    if (vkGetPhysicalDeviceImageFormatProperties2(m_physical, &info, &properties) != VK_SUCCESS)
        return false;
    return (externalProperties.externalMemoryProperties.externalMemoryFeatures & VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT) != 0;
}

bool VulkanPresenter::linearModifierSupported(VkFormat format)
{
    VkDrmFormatModifierPropertiesListEXT list{.sType = VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT};
    VkFormatProperties2 formatProperties{.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2, .pNext = &list};
    vkGetPhysicalDeviceFormatProperties2(m_physical, format, &formatProperties);
    std::vector<VkDrmFormatModifierPropertiesEXT> modifiers(list.drmFormatModifierCount);
    list.pDrmFormatModifierProperties = modifiers.data();
    vkGetPhysicalDeviceFormatProperties2(m_physical, format, &formatProperties);
    const bool linear = std::ranges::any_of(modifiers, [](const VkDrmFormatModifierPropertiesEXT& m) {
        return m.drmFormatModifier == DrmFormatModLinear && (m.drmFormatModifierTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);
    });
    if (!linear)
        return false;

    const VkPhysicalDeviceExternalImageFormatInfo external{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO,
        .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT,
    };
    const VkPhysicalDeviceImageDrmFormatModifierInfoEXT modifier{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_DRM_FORMAT_MODIFIER_INFO_EXT,
        .pNext = &external,
        .drmFormatModifier = DrmFormatModLinear,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    const VkPhysicalDeviceImageFormatInfo2 info{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
        .pNext = &modifier,
        .format = format,
        .type = VK_IMAGE_TYPE_2D,
        .tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT,
    };
    VkExternalImageFormatProperties externalProperties{.sType = VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES};
    VkImageFormatProperties2 properties{.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2, .pNext = &externalProperties};
    if (vkGetPhysicalDeviceImageFormatProperties2(m_physical, &info, &properties) != VK_SUCCESS)
        return false;
    return (externalProperties.externalMemoryProperties.externalMemoryFeatures & VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT) != 0;
}

bool VulkanPresenter::createSwapchain()
{
    VkSurfaceCapabilitiesKHR capabilities{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physical, m_surface, &capabilities);
    std::uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physical, m_surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physical, m_surface, &formatCount, formats.data());
    if (formats.empty())
    {
        m_log.line("FAIL no surface formats");
        return false;
    }
    m_format = formats.front().format;
    m_extent = capabilities.currentExtent;
    m_log.line("surface {}x{} format {} images {}..{}", m_extent.width, m_extent.height, static_cast<int>(m_format),
        capabilities.minImageCount, capabilities.maxImageCount);

    const VkSwapchainCreateInfoKHR info{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = m_surface,
        .minImageCount = std::max(3u, capabilities.minImageCount),
        .imageFormat = m_format,
        .imageColorSpace = formats.front().colorSpace,
        .imageExtent = m_extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR,
        .clipped = VK_TRUE,
    };
    if (const VkResult r = vkCreateSwapchainKHR(m_device, &info, nullptr, &m_swapchain); r != VK_SUCCESS)
    {
        m_log.line("FAIL vkCreateSwapchainKHR {}", static_cast<int>(r));
        return false;
    }
    std::uint32_t imageCount = 0;
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &imageCount, nullptr);
    m_images.resize(imageCount);
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &imageCount, m_images.data());
    for (VkImage image : m_images)
    {
        const VkImageViewCreateInfo view{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = m_format,
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
        };
        VkImageView created = VK_NULL_HANDLE;
        vkCreateImageView(m_device, &view, nullptr, &created);
        m_views.push_back(created);
        const VkSemaphoreCreateInfo semaphore{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkSemaphore rendered = VK_NULL_HANDLE;
        vkCreateSemaphore(m_device, &semaphore, nullptr, &rendered);
        m_rendered.push_back(rendered);
    }
    return true;
}

bool VulkanPresenter::createPipeline()
{
    const VkSamplerCreateInfo sampler{
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    };
    vkCreateSampler(m_device, &sampler, nullptr, &m_sampler);

    const std::array<VkDescriptorSetLayoutBinding, 2> bindings{{
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, &m_sampler},
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, &m_sampler},
    }};
    const VkDescriptorSetLayoutCreateInfo layout{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = static_cast<std::uint32_t>(bindings.size()),
        .pBindings = bindings.data(),
    };
    vkCreateDescriptorSetLayout(m_device, &layout, nullptr, &m_set_layout);

    const VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 * akira::stream::HardwareDecoder::ImageCount * 2};
    const VkDescriptorPoolCreateInfo pool{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
        .maxSets = akira::stream::HardwareDecoder::ImageCount * 2,
        .poolSizeCount = 1,
        .pPoolSizes = &poolSize,
    };
    vkCreateDescriptorPool(m_device, &pool, nullptr, &m_pool);

    const VkPushConstantRange push{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(float) * 4};
    const VkPipelineLayoutCreateInfo pipelineLayout{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &m_set_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push,
    };
    vkCreatePipelineLayout(m_device, &pipelineLayout, nullptr, &m_pipeline_layout);

    auto makeModule = [this](const std::uint32_t* code, std::size_t size) {
        const VkShaderModuleCreateInfo info{.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = size, .pCode = code};
        VkShaderModule created = VK_NULL_HANDLE;
        vkCreateShaderModule(m_device, &info, nullptr, &created);
        return created;
    };
    const VkShaderModule vertex = makeModule(fullscreen_vert, sizeof(fullscreen_vert));
    const VkShaderModule fragment = makeModule(nv12_frag, sizeof(nv12_frag));
    const std::array<VkPipelineShaderStageCreateInfo, 2> stages{{
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vertex, .pName = "main"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fragment, .pName = "main"},
    }};
    const VkPipelineVertexInputStateCreateInfo vertexInput{.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    const VkPipelineInputAssemblyStateCreateInfo assembly{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    const VkPipelineViewportStateCreateInfo viewport{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };
    const VkPipelineRasterizationStateCreateInfo raster{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .lineWidth = 1.0f,
    };
    const VkPipelineMultisampleStateCreateInfo multisample{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    const VkPipelineColorBlendAttachmentState attachment{.colorWriteMask = 0xF};
    const VkPipelineColorBlendStateCreateInfo blend{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &attachment,
    };
    const std::array dynamicStates{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    const VkPipelineDynamicStateCreateInfo dynamic{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size()),
        .pDynamicStates = dynamicStates.data(),
    };
    const VkPipelineRenderingCreateInfo rendering{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &m_format,
    };
    const VkGraphicsPipelineCreateInfo info{
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &rendering,
        .stageCount = static_cast<std::uint32_t>(stages.size()),
        .pStages = stages.data(),
        .pVertexInputState = &vertexInput,
        .pInputAssemblyState = &assembly,
        .pViewportState = &viewport,
        .pRasterizationState = &raster,
        .pMultisampleState = &multisample,
        .pColorBlendState = &blend,
        .pDynamicState = &dynamic,
        .layout = m_pipeline_layout,
    };
    const VkResult r = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &info, nullptr, &m_pipeline);
    vkDestroyShaderModule(m_device, vertex, nullptr);
    vkDestroyShaderModule(m_device, fragment, nullptr);
    if (r != VK_SUCCESS)
    {
        m_log.line("FAIL vkCreateGraphicsPipelines {}", static_cast<int>(r));
        return false;
    }
    return true;
}

bool VulkanPresenter::createFrames()
{
    const VkCommandPoolCreateInfo pool{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = m_queue_family,
    };
    vkCreateCommandPool(m_device, &pool, nullptr, &m_command_pool);
    for (InFlight& slot : m_in_flight)
    {
        const VkCommandBufferAllocateInfo allocate{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = m_command_pool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };
        vkAllocateCommandBuffers(m_device, &allocate, &slot.commands);
        const VkFenceCreateInfo fence{.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT};
        vkCreateFence(m_device, &fence, nullptr, &slot.fence);
        const VkSemaphoreCreateInfo semaphore{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        vkCreateSemaphore(m_device, &semaphore, nullptr, &slot.acquired);
    }
    return true;
}

std::optional<std::uint32_t> VulkanPresenter::memoryTypeFrom(std::uint32_t bits) const
{
    for (std::uint32_t i = 0; i < 32; i++)
        if (bits & (1u << i))
            return i;
    return std::nullopt;
}

bool VulkanPresenter::tryHostImport(const char* label, void* pointer, VkDeviceSize size, VkDeviceMemory* out)
{
    VkMemoryHostPointerPropertiesEXT properties{.sType = VK_STRUCTURE_TYPE_MEMORY_HOST_POINTER_PROPERTIES_EXT};
    const VkResult query = m_get_host_pointer_properties(m_device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT, pointer, &properties);
    if (query != VK_SUCCESS)
    {
        m_log.line("import {}: host pointer properties {}", label, static_cast<int>(query));
        return false;
    }
    const auto type = memoryTypeFrom(properties.memoryTypeBits);
    if (!type)
    {
        m_log.line("import {}: no memory type in bits 0x{:x}", label, properties.memoryTypeBits);
        return false;
    }
    const VkImportMemoryHostPointerInfoEXT import{
        .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT,
        .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT,
        .pHostPointer = pointer,
    };
    const VkMemoryAllocateInfo allocate{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = &import,
        .allocationSize = size,
        .memoryTypeIndex = *type,
    };
    const VkResult r = vkAllocateMemory(m_device, &allocate, nullptr, out);
    m_log.line("import {}: bits 0x{:x} allocate {}", label, properties.memoryTypeBits, static_cast<int>(r));
    return r == VK_SUCCESS;
}

std::optional<std::uint32_t> VulkanPresenter::hostVisibleType() const
{
    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(m_physical, &properties);
    for (std::uint32_t i = 0; i < properties.memoryTypeCount; ++i)
        if (properties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
            return i;
    return std::nullopt;
}

void VulkanPresenter::probeImportOrders()
{
    if (!m_host_import || !m_get_host_pointer_properties)
        return;
    constexpr std::size_t Size = 4 * 1024 * 1024;
    constexpr std::size_t Alignment = 0x10000;
    using Buffer = std::unique_ptr<void, decltype(&std::free)>;

    {
        Buffer heap(std::aligned_alloc(Alignment, Size), &std::free);
        VkDeviceMemory memory = VK_NULL_HANDLE;
        tryHostImport("A heap", heap.get(), Size, &memory);
        vkFreeMemory(m_device, memory, nullptr);
    }
    {
        Buffer heap(std::aligned_alloc(Alignment, Size), &std::free);
        GsvNativeMemory native{};
        const Result rc = gsvCreateNativeMemory(&native, heap.get(), Size);
        m_log.line("import B heap then native: gsvCreateNativeMemory 0x{:x} attribute {}", rc, native.has_attribute);
        VkDeviceMemory memory = VK_NULL_HANDLE;
        if (R_SUCCEEDED(rc))
        {
            tryHostImport("B heap then native", heap.get(), Size, &memory);
            vkFreeMemory(m_device, memory, nullptr);
            gsvDestroyNativeMemory(&native);
        }
    }
    {
        Buffer heap(std::aligned_alloc(Alignment, Size), &std::free);
        VkDeviceMemory memory = VK_NULL_HANDLE;
        if (tryHostImport("C import then native", heap.get(), Size, &memory))
        {
            GsvNativeMemory native{};
            const Result rc = gsvCreateNativeMemory(&native, heap.get(), Size);
            m_log.line("import C import then native: gsvCreateNativeMemory 0x{:x} attribute {}", rc, native.has_attribute);
            if (R_SUCCEEDED(rc))
                gsvDestroyNativeMemory(&native);
        }
        vkFreeMemory(m_device, memory, nullptr);
    }
    if (const auto type = hostVisibleType())
    {
        const VkMemoryAllocateInfo allocate{
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = Size,
            .memoryTypeIndex = *type,
        };
        VkDeviceMemory memory = VK_NULL_HANDLE;
        void* mapped = nullptr;
        const VkResult allocated = vkAllocateMemory(m_device, &allocate, nullptr, &memory);
        const VkResult map = allocated == VK_SUCCESS ? vkMapMemory(m_device, memory, 0, Size, 0, &mapped) : allocated;
        m_log.line("import D vulkan memory type {}: allocate {} map {} pointer 0x{:x}", *type, static_cast<int>(allocated),
            static_cast<int>(map), reinterpret_cast<std::uintptr_t>(mapped));
        if (mapped)
        {
            GsvNativeMemory native{};
            const Result rc = gsvCreateNativeMemory(&native, mapped, Size);
            m_log.line("import D vulkan memory then native: gsvCreateNativeMemory 0x{:x} attribute {}", rc, native.has_attribute);
            if (R_SUCCEEDED(rc))
                gsvDestroyNativeMemory(&native);
            vkUnmapMemory(m_device, memory);
        }
        vkFreeMemory(m_device, memory, nullptr);
    }
}

VulkanPresenter::SlotImport::SlotImport(VulkanPresenter& presenter, const akira::stream::HardwareFrame& frame)
    : m_presenter(presenter)
{
    ProbeLog& log = presenter.m_log;
    const auto address = reinterpret_cast<std::uintptr_t>(frame.backing());
    const VkDeviceSize alignment = std::max<VkDeviceSize>(presenter.m_host_alignment, 1);
    log.line("slot {}: backing 0x{:x} size {} luma off {} pitch {} chroma off {} pitch {} alignment ok {}/{}", frame.slot(), address,
        frame.backingSize(), frame.luma().offset, frame.luma().pitch, frame.chroma().offset, frame.chroma().pitch,
        address % alignment == 0, frame.backingSize() % alignment == 0);
    if (!presenter.m_host_import || !presenter.m_get_host_pointer_properties)
    {
        log.line("FAIL zero copy needs external_memory_host");
        return;
    }
    if (address % alignment != 0 || frame.backingSize() % alignment != 0)
    {
        log.line("FAIL backing is not aligned to minImportedHostPointerAlignment {}", alignment);
        return;
    }

    VkMemoryHostPointerPropertiesEXT hostProperties{.sType = VK_STRUCTURE_TYPE_MEMORY_HOST_POINTER_PROPERTIES_EXT};
    if (const VkResult r = presenter.m_get_host_pointer_properties(presenter.m_device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT,
            frame.backing(), &hostProperties);
        r != VK_SUCCESS)
    {
        log.line("FAIL vkGetMemoryHostPointerPropertiesEXT {}", static_cast<int>(r));
        return;
    }

    m_luma = createPlane(VK_FORMAT_R8_UNORM, 1, frame.width(), frame.height(), frame.luma());
    m_chroma = createPlane(VK_FORMAT_R8G8_UNORM, 2, (frame.width() + 1) / 2, (frame.height() + 1) / 2, frame.chroma());
    if (!m_luma || !m_chroma)
        return;
    if (!presenter.m_drm_modifier)
        m_uv_scale = static_cast<float>(frame.width()) / static_cast<float>(frame.luma().pitch);

    VkMemoryRequirements lumaRequirements{};
    VkMemoryRequirements chromaRequirements{};
    vkGetImageMemoryRequirements(presenter.m_device, m_luma, &lumaRequirements);
    vkGetImageMemoryRequirements(presenter.m_device, m_chroma, &chromaRequirements);
    const std::uint32_t bits = hostProperties.memoryTypeBits & lumaRequirements.memoryTypeBits & chromaRequirements.memoryTypeBits;
    const auto type = presenter.memoryTypeFrom(bits);
    log.line("slot {}: host bits 0x{:x} luma bits 0x{:x} size {} chroma bits 0x{:x} size {}", frame.slot(), hostProperties.memoryTypeBits,
        lumaRequirements.memoryTypeBits, lumaRequirements.size, chromaRequirements.memoryTypeBits, chromaRequirements.size);
    if (!type)
    {
        log.line("FAIL no memory type fits the imported host memory and both planes");
        return;
    }

    const VkImportMemoryHostPointerInfoEXT import{
        .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT,
        .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT,
        .pHostPointer = const_cast<void*>(frame.backing()),
    };
    const VkMemoryAllocateInfo allocate{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = &import,
        .allocationSize = frame.backingSize(),
        .memoryTypeIndex = *type,
    };
    if (const VkResult r = vkAllocateMemory(presenter.m_device, &allocate, nullptr, &m_memory); r != VK_SUCCESS)
    {
        log.line("FAIL vkAllocateMemory host import {}", static_cast<int>(r));
        return;
    }
    if (!bindPlane(m_luma, frame.luma(), 1, frame.backingSize(), "luma") ||
        !bindPlane(m_chroma, frame.chroma(), 2, frame.backingSize(), "chroma"))
        return;
    m_luma_view = createView(m_luma, VK_FORMAT_R8_UNORM);
    m_chroma_view = createView(m_chroma, VK_FORMAT_R8G8_UNORM);

    const VkDescriptorSetAllocateInfo setInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = presenter.m_pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &presenter.m_set_layout,
    };
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(presenter.m_device, &setInfo, &set) != VK_SUCCESS)
    {
        log.line("FAIL vkAllocateDescriptorSets");
        return;
    }
    const std::array<VkDescriptorImageInfo, 2> images{{
        {VK_NULL_HANDLE, m_luma_view, VK_IMAGE_LAYOUT_GENERAL},
        {VK_NULL_HANDLE, m_chroma_view, VK_IMAGE_LAYOUT_GENERAL},
    }};
    const std::array<VkWriteDescriptorSet, 2> writes{{
        {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = set, .dstBinding = 0, .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &images[0]},
        {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = set, .dstBinding = 1, .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &images[1]},
    }};
    vkUpdateDescriptorSets(presenter.m_device, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
    m_set = set;
    log.line("slot {}: zero-copy import OK via {} (uv scale {})", frame.slot(),
        presenter.m_drm_modifier ? "linear drm modifier images" : "linear tiled images", m_uv_scale);
}

VulkanPresenter::SlotImport::~SlotImport()
{
    const VkDevice device = m_presenter.m_device;
    if (m_set)
        vkFreeDescriptorSets(device, m_presenter.m_pool, 1, &m_set);
    vkDestroyImageView(device, m_luma_view, nullptr);
    vkDestroyImageView(device, m_chroma_view, nullptr);
    vkDestroyImage(device, m_luma, nullptr);
    vkDestroyImage(device, m_chroma, nullptr);
    vkFreeMemory(device, m_memory, nullptr);
}

bool VulkanPresenter::SlotImport::bindPlane(VkImage image, const akira::stream::HardwarePlane& plane, std::uint32_t texelSize,
    VkDeviceSize backingSize, const char* name)
{
    ProbeLog& log = m_presenter.m_log;
    const VkDevice device = m_presenter.m_device;
    if (m_presenter.m_drm_modifier)
    {
        if (vkBindImageMemory(device, image, m_memory, 0) == VK_SUCCESS)
            return true;
        log.line("FAIL vkBindImageMemory {}", name);
        return false;
    }

    const VkImageSubresource subresource{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0};
    VkSubresourceLayout layout{};
    vkGetImageSubresourceLayout(device, image, &subresource, &layout);
    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device, image, &requirements);
    log.line("{}: linear row pitch {} want {} offset {} size {} alignment {} bind at {}", name, layout.rowPitch, plane.pitch,
        layout.offset, requirements.size, requirements.alignment, plane.offset);
    if (layout.rowPitch != plane.pitch || layout.offset != 0)
    {
        log.line("FAIL {} linear layout does not match the decoder (texel size {})", name, texelSize);
        return false;
    }
    if (plane.offset % std::max<VkDeviceSize>(requirements.alignment, 1) != 0 || plane.offset + requirements.size > backingSize)
    {
        log.line("FAIL {} plane offset {} is misaligned or runs past the backing", name, plane.offset);
        return false;
    }
    if (const VkResult r = vkBindImageMemory(device, image, m_memory, plane.offset); r != VK_SUCCESS)
    {
        log.line("FAIL vkBindImageMemory {} at {}: {}", name, plane.offset, static_cast<int>(r));
        return false;
    }
    return true;
}

VkImage VulkanPresenter::SlotImport::createPlane(VkFormat format, std::uint32_t texelSize, std::uint32_t width, std::uint32_t height,
    const akira::stream::HardwarePlane& plane)
{
    if (!m_presenter.m_drm_modifier)
    {
        const VkExternalMemoryImageCreateInfo external{
            .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
            .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT,
        };
        const VkImageCreateInfo info{
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .pNext = &external,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = format,
            .extent = {static_cast<std::uint32_t>(plane.pitch / texelSize), height, 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_LINEAR,
            .usage = VK_IMAGE_USAGE_SAMPLED_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };
        VkImage image = VK_NULL_HANDLE;
        if (const VkResult r = vkCreateImage(m_presenter.m_device, &info, nullptr, &image); r != VK_SUCCESS)
        {
            m_presenter.m_log.line("FAIL vkCreateImage {} linear {}x{} pitch {}: {}", static_cast<int>(format), info.extent.width, height,
                plane.pitch, static_cast<int>(r));
            return VK_NULL_HANDLE;
        }
        return image;
    }

    const VkSubresourceLayout layout{.offset = plane.offset, .rowPitch = plane.pitch};
    const VkImageDrmFormatModifierExplicitCreateInfoEXT modifier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT,
        .drmFormatModifier = DrmFormatModLinear,
        .drmFormatModifierPlaneCount = 1,
        .pPlaneLayouts = &layout,
    };
    const VkExternalMemoryImageCreateInfo external{
        .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
        .pNext = &modifier,
        .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT,
    };
    const VkImageCreateInfo info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = &external,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format,
        .extent = {width, height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VkImage image = VK_NULL_HANDLE;
    if (const VkResult r = vkCreateImage(m_presenter.m_device, &info, nullptr, &image); r != VK_SUCCESS)
    {
        m_presenter.m_log.line("FAIL vkCreateImage {} linear modifier {}x{} offset {} pitch {}: {}", static_cast<int>(format), width,
            height, plane.offset, plane.pitch, static_cast<int>(r));
        return VK_NULL_HANDLE;
    }
    return image;
}

VkImageView VulkanPresenter::SlotImport::createView(VkImage image, VkFormat format)
{
    const VkImageViewCreateInfo info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = format,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };
    VkImageView view = VK_NULL_HANDLE;
    vkCreateImageView(m_presenter.m_device, &info, nullptr, &view);
    return view;
}

VulkanPresenter::SlotImport* VulkanPresenter::importFor(const akira::stream::HardwareFrame& frame)
{
    if (frame.generation() != m_generation)
    {
        for (auto& import : m_imports)
            if (import)
                m_retired.push_back(std::move(import));
        m_generation = frame.generation();
    }
    auto& import = m_imports[frame.slot()];
    if (!import)
        import = std::make_unique<SlotImport>(*this, frame);
    return import->valid() ? import.get() : nullptr;
}

void VulkanPresenter::recordFrame(InFlight& slot, std::uint32_t imageIndex, SlotImport& import, const akira::stream::HardwareFrame& frame)
{
    const VkCommandBufferBeginInfo begin{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(slot.commands, &begin);

    const VkImageLayout planeOld = import.initialized() ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED;
    auto planeBarrier = [&](VkImage image) {
        return VkImageMemoryBarrier2{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_2_HOST_BIT,
            .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_HOST_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
            .oldLayout = planeOld,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image,
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
        };
    };
    const std::array<VkImageMemoryBarrier2, 3> before{{
        planeBarrier(import.luma()),
        planeBarrier(import.chroma()),
        {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = 0,
            .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = m_images[imageIndex],
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
        },
    }};
    const VkDependencyInfo beforeInfo{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = static_cast<std::uint32_t>(before.size()),
        .pImageMemoryBarriers = before.data(),
    };
    vkCmdPipelineBarrier2(slot.commands, &beforeInfo);
    import.markInitialized();

    const VkRenderingAttachmentInfo color{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = m_views[imageIndex],
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = {.color = {{0.0f, 0.0f, 0.0f, 1.0f}}},
    };
    const VkRenderingInfo rendering{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {{0, 0}, m_extent},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color,
    };
    vkCmdBeginRendering(slot.commands, &rendering);

    const float frameAspect = static_cast<float>(frame.width()) / static_cast<float>(frame.height());
    const float screenAspect = static_cast<float>(m_extent.width) / static_cast<float>(m_extent.height);
    float width = static_cast<float>(m_extent.width);
    float height = static_cast<float>(m_extent.height);
    if (frameAspect > screenAspect)
        height = width / frameAspect;
    else
        width = height * frameAspect;
    const VkViewport viewport{(m_extent.width - width) / 2.0f, (m_extent.height - height) / 2.0f, width, height, 0.0f, 1.0f};
    const VkRect2D scissor{{0, 0}, m_extent};
    vkCmdSetViewport(slot.commands, 0, 1, &viewport);
    vkCmdSetScissor(slot.commands, 0, 1, &scissor);
    vkCmdBindPipeline(slot.commands, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    const VkDescriptorSet set = import.set();
    vkCmdBindDescriptorSets(slot.commands, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline_layout, 0, 1, &set, 0, nullptr);
    const std::array<float, 4> push{import.uvScale(), 1.0f, 0.0f, 0.0f};
    vkCmdPushConstants(slot.commands, m_pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), push.data());
    vkCmdDraw(slot.commands, 3, 1, 0, 0);
    vkCmdEndRendering(slot.commands);

    const VkImageMemoryBarrier2 toPresent{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_NONE,
        .dstAccessMask = 0,
        .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = m_images[imageIndex],
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };
    const VkDependencyInfo afterInfo{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &toPresent,
    };
    vkCmdPipelineBarrier2(slot.commands, &afterInfo);
    vkEndCommandBuffer(slot.commands);
}

bool VulkanPresenter::present(const akira::stream::HardwareDecoder::Frame& frame)
{
    InFlight& slot = m_in_flight[m_current];
    vkWaitForFences(m_device, 1, &slot.fence, VK_TRUE, UINT64_MAX);
    slot.frames.clear();

    SlotImport* import = importFor(*frame);
    if (!import)
        return false;

    std::uint32_t imageIndex = 0;
    if (const VkResult r = vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX, slot.acquired, VK_NULL_HANDLE, &imageIndex);
        r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR)
    {
        m_log.line("FAIL vkAcquireNextImageKHR {}", static_cast<int>(r));
        return false;
    }
    vkResetFences(m_device, 1, &slot.fence);
    vkResetCommandBuffer(slot.commands, 0);
    recordFrame(slot, imageIndex, *import, *frame);
    slot.frames.push_back(frame);

    const VkSemaphoreSubmitInfo wait{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = slot.acquired,
        .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
    };
    const VkSemaphoreSubmitInfo signal{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = m_rendered[imageIndex],
        .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
    };
    const VkCommandBufferSubmitInfo commands{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, .commandBuffer = slot.commands};
    const VkSubmitInfo2 submit{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .waitSemaphoreInfoCount = 1,
        .pWaitSemaphoreInfos = &wait,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &commands,
        .signalSemaphoreInfoCount = 1,
        .pSignalSemaphoreInfos = &signal,
    };
    if (const VkResult r = vkQueueSubmit2(m_queue, 1, &submit, slot.fence); r != VK_SUCCESS)
    {
        m_log.line("FAIL vkQueueSubmit2 {}", static_cast<int>(r));
        return false;
    }
    const VkPresentInfoKHR present{
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &m_rendered[imageIndex],
        .swapchainCount = 1,
        .pSwapchains = &m_swapchain,
        .pImageIndices = &imageIndex,
    };
    if (const VkResult r = vkQueuePresentKHR(m_queue, &present); r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR)
    {
        m_log.line("FAIL vkQueuePresentKHR {}", static_cast<int>(r));
        return false;
    }
    m_current = (m_current + 1) % FramesInFlight;
    return true;
}
