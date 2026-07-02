#include <cstdint>
#include <stdexcept>
#include <print>
#include <algorithm>
#include <array>
#include <fstream>
#include <variant>

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/hash.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <fastgltf/types.hpp>
#include <fastgltf/util.hpp>
#include <string>

#include "VulkanInterface.hpp"
#include "vertex.hpp"

struct UniformBufferObject {
	alignas(16) glm::mat4 model;
	alignas(16) glm::mat4 view;
	alignas(16) glm::mat4 proj;
};

VulkanInterface::VulkanInterface() {
    initWindow();
    initVulkan();
}

VulkanInterface::~VulkanInterface() {
    cleanup();
}

void VulkanInterface::initWindow() {
    glfwInit();

    auto const monitor {glfwGetPrimaryMonitor()};
    const GLFWvidmode* mode {glfwGetVideoMode(monitor)};

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    glfwWindowHint(GLFW_RED_BITS, mode->redBits);
    glfwWindowHint(GLFW_GREEN_BITS, mode->greenBits);
    glfwWindowHint(GLFW_BLUE_BITS, mode->blueBits);
    glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);

    m_window = glfwCreateWindow(mode->width, mode->height, "My Game", nullptr, NULL);
}
void VulkanInterface::initVulkan() {
    createInstance();
    createSurface();
    pickPhysicalDevice();
    createLogicalDevice();
    createVmaAllocator();
    createSwapChain();
    createSwapChainImageViews();
    createDescriptorSetLayout();
    createGraphicsPipeline();
    createCommandPool(); 
    createDepthResources();
    createCommandBuffers();
    createSyncObjects();
}
void VulkanInterface::loadScene(const fastgltf::Asset& asset) {
    createTextureImages(asset);
    createTextureSamplers(asset);
    loadModels(asset);
}
void VulkanInterface::cleanup() {
    vmaDestroyImage(m_allocator, *m_depthImage, m_depthImageAllocation);
    for (size_t i {0}; i < m_textureImages.size(); ++i) {
        vmaDestroyImage(m_allocator, *m_textureImages[i], m_textureImageAllocations[i]);
    }
    glfwDestroyWindow(m_window);
    glfwTerminate();
}

void VulkanInterface::createInstance() {
    constexpr vk::ApplicationInfo appInfo {
        .pApplicationName = "My Game",
        .applicationVersion = VK_MAKE_VERSION(0, 1, 0),
        .pEngineName = "Wide Engine",
        .engineVersion = VK_MAKE_VERSION(0, 1, 0),
        .apiVersion = vk::ApiVersion14
    };

    uint32_t glfwExtensionCount {0};
    auto glfwExtensions {glfwGetRequiredInstanceExtensions(&glfwExtensionCount)};
    std::vector<const char*> requiredGLFWExtensions {glfwExtensions, glfwExtensions + glfwExtensionCount};

    auto supportedVulkanExtensions {m_context.enumerateInstanceExtensionProperties()};

    std::string unsupportedExtension {""};
    for (const auto& requiredGLFWExtension: requiredGLFWExtensions) {
        bool found {false};
        for (const auto& supportedVulkanExtension: supportedVulkanExtensions) {
            if (strcmp(requiredGLFWExtension, supportedVulkanExtension.extensionName) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            unsupportedExtension = requiredGLFWExtension;
            break;
        }
    }
    if (unsupportedExtension != "") {
        throw std::runtime_error("Required extension not supported: " + unsupportedExtension);
    }

    vk::InstanceCreateInfo instanceCreateInfo {
        .pApplicationInfo = &appInfo,
        .enabledExtensionCount = static_cast<uint32_t>(requiredGLFWExtensions.size()),
        .ppEnabledExtensionNames = requiredGLFWExtensions.data()
    };
    m_instance = {m_context, instanceCreateInfo};
}

void VulkanInterface::createSurface() {
    VkSurfaceKHR _surface {};
    if (glfwCreateWindowSurface(*m_instance, m_window, nullptr, &_surface) != 0) {
        throw std::runtime_error("Failed to create window surface!");
    }
    m_surface = {m_instance, _surface};
}

bool VulkanInterface::isDeviceSuitable(const vk::raii::PhysicalDevice& physicalDevice) const {
    bool supportsVulkan1_3 {physicalDevice.getProperties().apiVersion >= vk::ApiVersion13};

    auto queueFamilies {physicalDevice.getQueueFamilyProperties()};
    bool queueFamilySupportsGraphics {std::ranges::any_of(queueFamilies, [](const auto& queueFamily) {
        return static_cast<bool>(queueFamily.queueFlags & vk::QueueFlagBits::eGraphics);
    })};

    std::vector<std::string> requiredDeviceExtensions {vk::KHRSwapchainExtensionName};
    auto availableDeviceExtensions {physicalDevice.enumerateDeviceExtensionProperties()};

    bool supportsAllExtensions {true};
    for (const auto& requiredDeviceExtension: requiredDeviceExtensions) {
        bool supportsExtension {false};
        for (const auto& availableDeviceExtension: availableDeviceExtensions) {
            if (requiredDeviceExtension == availableDeviceExtension.extensionName) {
                supportsExtension = true;
                break;
            }
        }
        if (!supportsExtension) {
            supportsAllExtensions = false;
            break;
        }
    }

    auto features {physicalDevice.template getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan11Features, vk::PhysicalDeviceVulkan13Features, vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>()};
    bool supportsRequiredFeatures {
        features.template get<vk::PhysicalDeviceFeatures2>().features.samplerAnisotropy and
        features.template get<vk::PhysicalDeviceVulkan11Features>().shaderDrawParameters and
        features.template get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering and
        features.template get<vk::PhysicalDeviceVulkan13Features>().synchronization2 and
        features.template get<vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>().extendedDynamicState
    };

    return supportsVulkan1_3 and queueFamilySupportsGraphics and supportsAllExtensions and supportsRequiredFeatures;
}
void VulkanInterface::pickPhysicalDevice() {
    m_availablePhysicalDevices = m_instance.enumeratePhysicalDevices();
    for (const auto& physicalDevice: m_availablePhysicalDevices) {
        std::println("{}", static_cast<std::string>(physicalDevice.getProperties().deviceName));
    }
    auto const devicesIt {std::ranges::find_if(m_availablePhysicalDevices, [&](auto const& physicalDevice) {return isDeviceSuitable(physicalDevice);})};
    if (devicesIt == m_availablePhysicalDevices.end()) {
        throw std::runtime_error("Failed to find a suitable GPU!");
    }
    m_physicalDevice = *devicesIt;
}
void VulkanInterface::createLogicalDevice() {
    std::vector<vk::QueueFamilyProperties> queueFamilies {m_physicalDevice.getQueueFamilyProperties()};
    for (uint32_t queueFamilyIndex = 0; queueFamilyIndex < queueFamilies.size(); ++queueFamilyIndex) {
        if ((queueFamilies[queueFamilyIndex].queueFlags & vk::QueueFlagBits::eGraphics) and m_physicalDevice.getSurfaceSupportKHR(queueFamilyIndex, *m_surface)) {
            m_queueFamilyIndex = queueFamilyIndex;
            break;
        }
    } // find suitable queue family that supports graphics and present
    if (m_queueFamilyIndex == ~0) {
        throw std::runtime_error("Could not find a queue for graphics and present -> terminating");
    }

    // query for Vulkan 1.3 features
    vk::StructureChain<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan13Features, vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT> featureChain {
        {.features = {.samplerAnisotropy = true}}, // vk::PhysicalDeviceFeatures2
        {.synchronization2 = true, .dynamicRendering = true}, // vk::PhysicalDeviceVulkan13Features
        {.extendedDynamicState = true} // vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT
    };

    // create a device
    float queuePriority {0.5};
    vk::DeviceQueueCreateInfo deviceQueueCreateInfo {
        .queueFamilyIndex = m_queueFamilyIndex,
        .queueCount = 1,
        .pQueuePriorities = &queuePriority
    };
    vk::DeviceCreateInfo deviceCreateInfo {
        .pNext = &(featureChain.get<vk::PhysicalDeviceFeatures2>()),
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &deviceQueueCreateInfo,
        .enabledExtensionCount = static_cast<uint32_t>(m_requiredDeviceExtensions.size()),
        .ppEnabledExtensionNames = m_requiredDeviceExtensions.data()
    };
    m_device = {m_physicalDevice, deviceCreateInfo};
    m_queue = {m_device, m_queueFamilyIndex, 0};
}

void VulkanInterface::createVmaAllocator() {
    VmaAllocatorCreateInfo allocatorCreateInfo {
        .physicalDevice = *m_physicalDevice,
        .device = *m_device,
        .instance = *m_instance,
        .vulkanApiVersion = VK_API_VERSION_1_4
    };
    vmaCreateAllocator(&allocatorCreateInfo, &m_allocator);
}

vk::Extent2D VulkanInterface::chooseSwapExtent(const vk::SurfaceCapabilitiesKHR& surfaceCapabilities) const {
    if (surfaceCapabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return surfaceCapabilities.currentExtent;
    }
    int width {};
    int height {};
    glfwGetFramebufferSize(m_window, &width, &height);

    return {
        std::clamp<uint32_t>(width, surfaceCapabilities.minImageExtent.width, surfaceCapabilities.maxImageExtent.width),
        std::clamp<uint32_t>(height, surfaceCapabilities.minImageExtent.height, surfaceCapabilities.maxImageExtent.height)
    };
}
uint32_t VulkanInterface::chooseSwapMinImageCount(const vk::SurfaceCapabilitiesKHR& surfaceCapabilities) {
    auto minImageCount {std::max(3u, surfaceCapabilities.minImageCount)};
    if ((0 < surfaceCapabilities.maxImageCount) and (surfaceCapabilities.maxImageCount < minImageCount)) {
        minImageCount = surfaceCapabilities.maxImageCount;
    }
    return minImageCount;
}
vk::SurfaceFormatKHR VulkanInterface::chooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& availableFormats) {
    assert(!availableFormats.empty());
    const auto formatIt {std::ranges::find_if(availableFormats, [](const auto& availableFormat) {
        return availableFormat.format == vk::Format::eB8G8R8A8Srgb and availableFormat.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear;
    })};
    return formatIt != availableFormats.end() ? *formatIt : availableFormats[0];
}
void VulkanInterface::createSwapChain() {
    vk::SurfaceCapabilitiesKHR surfaceCapabilities {m_physicalDevice.getSurfaceCapabilitiesKHR(m_surface)};
    m_swapChainExtent = chooseSwapExtent(surfaceCapabilities);
    uint32_t minImageCount {chooseSwapMinImageCount(surfaceCapabilities)};

    std::vector<vk::SurfaceFormatKHR> availableFormats {m_physicalDevice.getSurfaceFormatsKHR(m_surface)};
    m_swapChainSurfaceFormat = chooseSwapSurfaceFormat(availableFormats);

    std::vector<vk::PresentModeKHR> availablePresentModes {m_physicalDevice.getSurfacePresentModesKHR(m_surface)};
    vk::PresentModeKHR presentMode {vk::PresentModeKHR::eFifo}; // vsync on by default
    
    vk::SwapchainCreateInfoKHR swapChainCreateInfo {
        .surface = m_surface,
        .minImageCount = minImageCount,
        .imageFormat = m_swapChainSurfaceFormat.format,
        .imageColorSpace = m_swapChainSurfaceFormat.colorSpace,
        .imageExtent = m_swapChainExtent,
        .imageArrayLayers = 1,
        .imageUsage = vk::ImageUsageFlagBits::eColorAttachment,
        .imageSharingMode = vk::SharingMode::eExclusive,
        .preTransform = surfaceCapabilities.currentTransform,
        .compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque,
        .presentMode = presentMode,
        .clipped = true
    };
    m_swapChain = {m_device, swapChainCreateInfo};
    m_swapChainImages = m_swapChain.getImages();
}
void VulkanInterface::createSwapChainImageViews() {
    assert(m_swapChainImageViews.empty());

    vk::ImageViewCreateInfo imageViewCreateInfo {
        .viewType = vk::ImageViewType::e2D,
        .format = m_swapChainSurfaceFormat.format,
        .subresourceRange = {
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };
    for (auto& image: m_swapChainImages) {
        imageViewCreateInfo.image = image;
        m_swapChainImageViews.emplace_back(m_device, imageViewCreateInfo);
    }
}

void VulkanInterface::createDescriptorSetLayout() {
    std::array bindings {
        vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eVertex, nullptr),
        vk::DescriptorSetLayoutBinding(1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment, nullptr)
    };
    vk::DescriptorSetLayoutCreateInfo layoutInfo {
        .bindingCount = static_cast<uint32_t>(bindings.size()),
        .pBindings = bindings.data(),
    };
    m_descriptorSetLayout = {m_device, layoutInfo};
}

std::vector<char> VulkanInterface::readFile(const std::string& filename) {
    std::ifstream file {filename, std::ios::ate | std::ios::binary};
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file!");
    }
    std::vector<char> buffer (file.tellg());
    file.seekg(0, std::ios::beg);
    file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    file.close();
    return buffer;
}
[[nodiscard]] vk::raii::ShaderModule VulkanInterface::createShaderModule(const std::vector<char>& code) const {
    vk::ShaderModuleCreateInfo createInfo {.codeSize = code.size(), .pCode = reinterpret_cast<const uint32_t *>(code.data())};
    return {m_device, createInfo};
}
vk::Format VulkanInterface::findSupportedFormat(const std::vector<vk::Format>& candidates, vk::ImageTiling tiling, vk::FormatFeatureFlags features) const {
    for (const auto format: candidates) {
        vk::FormatProperties props {m_physicalDevice.getFormatProperties(format)};

        if (tiling == vk::ImageTiling::eLinear and (props.linearTilingFeatures & features) == features) {
            return format;
        }
        if (tiling == vk::ImageTiling::eOptimal and (props.optimalTilingFeatures & features) == features) {
            return format;
        }
    }

    throw std::runtime_error("Failed to find supported format!");
}
[[nodiscard]] vk::Format VulkanInterface::findDepthFormat() const {
    return findSupportedFormat(
        {vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint, vk::Format::eD24UnormS8Uint},
        vk::ImageTiling::eOptimal,
        vk::FormatFeatureFlagBits::eDepthStencilAttachment
    );
}
void VulkanInterface::createGraphicsPipeline() {
    vk::raii::ShaderModule shaderModule {createShaderModule(readFile(SHADER_PATH"/slang.spv"))};

    vk::PipelineShaderStageCreateInfo vertShaderStageInfo {
        .stage = vk::ShaderStageFlagBits::eVertex,
        .module = shaderModule,
        .pName = "vertMain"
    };
    vk::PipelineShaderStageCreateInfo fragShaderStageInfo {
        .stage = vk::ShaderStageFlagBits::eFragment,
        .module = shaderModule,
        .pName = "fragMain"
    };
    vk::PipelineShaderStageCreateInfo shaderStages[] {vertShaderStageInfo, fragShaderStageInfo};

    auto bindingDescription {Vertex::getBindingDescription()};
    auto attributeDescriptions {Vertex::getAttributeDescriptions()};
    vk::PipelineVertexInputStateCreateInfo vertexInputInfo {
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &bindingDescription,
        .vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size()),
        .pVertexAttributeDescriptions = attributeDescriptions.data()
    };

    vk::PipelineInputAssemblyStateCreateInfo inputAssembly {
        .topology = vk::PrimitiveTopology::eTriangleList,
        .primitiveRestartEnable = vk::False
    };

    vk::PipelineViewportStateCreateInfo viewportState {
        .viewportCount = 1,
        .scissorCount = 1
    };

    vk::PipelineRasterizationStateCreateInfo rasterizer {
        .depthClampEnable = vk::False,
        .rasterizerDiscardEnable = vk::False,
        .polygonMode = vk::PolygonMode::eFill,
        .cullMode = vk::CullModeFlagBits::eBack,
        .frontFace = vk::FrontFace::eCounterClockwise,
        .depthBiasEnable = vk::False,
        .lineWidth = 1.0f
    };

    vk::PipelineMultisampleStateCreateInfo multisampling {
        .rasterizationSamples = vk::SampleCountFlagBits::e1,
        .sampleShadingEnable = vk::False
    };

    vk::PipelineDepthStencilStateCreateInfo depthStencil {
        .depthTestEnable = vk::True,
        .depthWriteEnable = vk::True,
        .depthCompareOp = vk::CompareOp::eLess,
        .depthBoundsTestEnable = vk::False,
        .stencilTestEnable = vk::False
    };

    vk::PipelineColorBlendAttachmentState colorBlendAttachment {
        .blendEnable = vk::False,
        .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA
    };

    vk::PipelineColorBlendStateCreateInfo colorBlending {
        .logicOpEnable = vk::False,
        .logicOp = vk::LogicOp::eCopy,
        .attachmentCount = 1,
        .pAttachments = &colorBlendAttachment
    };

    std::vector dynamicStates {
        vk::DynamicState::eViewport,
        vk::DynamicState::eScissor
    };

    vk::PipelineDynamicStateCreateInfo dynamicState{
        .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
        .pDynamicStates = dynamicStates.data()
    };

    vk::PipelineLayoutCreateInfo pipelineLayoutInfo {
        .setLayoutCount = 1,
        .pSetLayouts = &*m_descriptorSetLayout,
        .pushConstantRangeCount = 0
    };

    m_pipelineLayout = {m_device, pipelineLayoutInfo};

    vk::Format depthFormat {findDepthFormat()};

    vk::StructureChain<vk::GraphicsPipelineCreateInfo, vk::PipelineRenderingCreateInfo> pipelineCreateInfoChain {
        {
            .stageCount = 2,
            .pStages = shaderStages,
            .pVertexInputState = &vertexInputInfo,
            .pInputAssemblyState = &inputAssembly,
            .pViewportState = &viewportState,
            .pRasterizationState = &rasterizer,
            .pMultisampleState = &multisampling,
            .pDepthStencilState = &depthStencil,
            .pColorBlendState = &colorBlending,
            .pDynamicState = &dynamicState,
            .layout = m_pipelineLayout,
            .renderPass = nullptr
        },
        {
            .colorAttachmentCount = 1,
            .pColorAttachmentFormats = &m_swapChainSurfaceFormat.format,
            .depthAttachmentFormat = depthFormat
        }
    };
    m_graphicsPipeline = {m_device, nullptr, pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>()};
}

void VulkanInterface::createCommandPool() {
    vk::CommandPoolCreateInfo poolInfo {
        .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
        .queueFamilyIndex = m_queueFamilyIndex
    };
    m_commandPool = {m_device, poolInfo};
}

uint32_t VulkanInterface::findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties) const {
    vk::PhysicalDeviceMemoryProperties memProperties {m_physicalDevice.getMemoryProperties()};
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
        if ((typeFilter & (1 << i)) and (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    throw std::runtime_error("failed to find suitable memory type!");
}
void VulkanInterface::createImage(uint32_t width, uint32_t height, uint32_t mipLevels, vk::Format format, vk::ImageTiling tiling, vk::ImageUsageFlags usage, vk::raii::Image& image, VmaAllocation& allocation) const {
    vk::ImageCreateInfo imageInfo{
        .imageType     = vk::ImageType::e2D,
        .format        = format,
        .extent        = {width, height, 1},
        .mipLevels     = mipLevels,
        .arrayLayers   = 1,
        .samples       = vk::SampleCountFlagBits::e1,
        .tiling        = tiling,
        .usage         = usage,
        .sharingMode   = vk::SharingMode::eExclusive,
        .initialLayout = vk::ImageLayout::eUndefined
    };

    VmaAllocationCreateInfo allocInfo {};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    VkImage imageTemp {};
    vmaCreateImage(m_allocator, &*imageInfo, &allocInfo, &imageTemp, &allocation, nullptr);
    image = {m_device, imageTemp};
}
[[nodiscard]] vk::raii::ImageView VulkanInterface::createImageView(const vk::raii::Image& image, vk::Format format, vk::ImageAspectFlags aspectFlags, uint32_t mipLevels) const {
    vk::ImageViewCreateInfo viewInfo{
        .image            = image,
        .viewType         = vk::ImageViewType::e2D,
        .format           = format,
        .subresourceRange = {aspectFlags, 0, mipLevels, 0, 1}};
    return vk::raii::ImageView{m_device, viewInfo};
}
void VulkanInterface::createDepthResources() {
    vk::Format depthFormat {findDepthFormat()};

    createImage(m_swapChainExtent.width, m_swapChainExtent.height, 1, depthFormat, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eDepthStencilAttachment, m_depthImage, m_depthImageAllocation);
    m_depthImageView = createImageView(m_depthImage, depthFormat, vk::ImageAspectFlagBits::eDepth, 1);
}

void VulkanInterface::createBuffer(vk::DeviceSize size, vk::BufferUsageFlags usage, vk::raii::Buffer& buffer, VmaAllocation& allocation) const {
    VkBufferCreateInfo bufferInfo {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = size;
    bufferInfo.usage = static_cast<VkBufferUsageFlags>(usage);
    
    VmaAllocationCreateInfo allocInfo {};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    
    VkBuffer bufferTemp;
    vmaCreateBuffer(m_allocator, &bufferInfo, &allocInfo, &bufferTemp, &allocation, nullptr);
    buffer = {m_device, bufferTemp};
}

void VulkanInterface::createCommandBuffers() {
    m_commandBuffers.clear();
    vk::CommandBufferAllocateInfo allocInfo{
        .commandPool = m_commandPool,
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = MAX_FRAMES_IN_FLIGHT
    };
    m_commandBuffers = vk::raii::CommandBuffers{m_device, allocInfo};
}

std::unique_ptr<vk::raii::CommandBuffer> VulkanInterface::beginSingleTimeCommands() const {
    vk::CommandBufferAllocateInfo allocInfo {
        .commandPool        = m_commandPool,
        .level              = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = 1
    };
    std::unique_ptr<vk::raii::CommandBuffer> commandBuffer = std::make_unique<vk::raii::CommandBuffer>(std::move(vk::raii::CommandBuffers(m_device, allocInfo).front()));

    vk::CommandBufferBeginInfo beginInfo {
        .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit
    };
    commandBuffer->begin(beginInfo);

    return commandBuffer;
}
void VulkanInterface::endSingleTimeCommands(const vk::raii::CommandBuffer& commandBuffer) const {
    commandBuffer.end();

    vk::SubmitInfo submitInfo{.commandBufferCount = 1, .pCommandBuffers = &*commandBuffer};
    m_queue.submit(submitInfo, nullptr);
    m_queue.waitIdle();
}

void VulkanInterface::createSyncObjects() {
    assert(m_presentCompleteSemaphores.empty() and m_renderFinishedSemaphores.empty() and m_inFlightFences.empty());
    for (size_t i {0}; i < m_swapChainImages.size(); ++i) {
        m_renderFinishedSemaphores.emplace_back(m_device, vk::SemaphoreCreateInfo());
    }
    for (size_t i {0}; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        m_presentCompleteSemaphores.emplace_back(m_device, vk::SemaphoreCreateInfo());
        m_inFlightFences.emplace_back(m_device, vk::FenceCreateInfo{.flags = vk::FenceCreateFlagBits::eSignaled});
    }
}


void VulkanInterface::transitionImageLayout(const vk::raii::Image& image, const vk::ImageLayout oldLayout, const vk::ImageLayout newLayout, uint32_t mipLevels) const {
    const auto commandBuffer {beginSingleTimeCommands()};

    vk::ImageMemoryBarrier barrier {
        .oldLayout = oldLayout,
        .newLayout = newLayout,
        .image = image,
        .subresourceRange = {
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .baseMipLevel = 0,
            .levelCount = mipLevels,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };

    vk::PipelineStageFlags sourceStage {};
    vk::PipelineStageFlags destinationStage {};

    if (oldLayout == vk::ImageLayout::eUndefined and newLayout == vk::ImageLayout::eTransferDstOptimal) {
        barrier.srcAccessMask = {};
        barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;

        sourceStage      = vk::PipelineStageFlagBits::eTopOfPipe;
        destinationStage = vk::PipelineStageFlagBits::eTransfer;
    }
    else if (oldLayout == vk::ImageLayout::eTransferDstOptimal and newLayout == vk::ImageLayout::eShaderReadOnlyOptimal) {
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

        sourceStage      = vk::PipelineStageFlagBits::eTransfer;
        destinationStage = vk::PipelineStageFlagBits::eFragmentShader;
    }
    else {
        throw std::invalid_argument("unsupported layout transition!");
    }

    commandBuffer->pipelineBarrier(sourceStage, destinationStage, {}, {}, nullptr, barrier);
    endSingleTimeCommands(*commandBuffer);
}
void VulkanInterface::copyBufferToImage(const vk::raii::Buffer& buffer, const vk::raii::Image& image, uint32_t width, uint32_t height) const {
    std::unique_ptr<vk::raii::CommandBuffer> commandBuffer = beginSingleTimeCommands();
    vk::BufferImageCopy region {
        .bufferOffset = 0,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
        .imageOffset = {0, 0, 0},
        .imageExtent = {width, height, 1}
    };
    commandBuffer->copyBufferToImage(buffer, image, vk::ImageLayout::eTransferDstOptimal, {region});
    endSingleTimeCommands(*commandBuffer);
}
void VulkanInterface::generateMipmaps(vk::raii::Image& image, vk::Format imageFormat, int32_t texWidth, int32_t texHeight, uint32_t mipLevels) const {
    // Check if image format supports linear blit-ing
    vk::FormatProperties formatProperties = m_physicalDevice.getFormatProperties(imageFormat);

    if (!(formatProperties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eSampledImageFilterLinear)) {throw std::runtime_error("texture image format does not support linear blitting!");}

    std::unique_ptr<vk::raii::CommandBuffer> commandBuffer = beginSingleTimeCommands();

    vk::ImageMemoryBarrier barrier = {
        .srcAccessMask = vk::AccessFlagBits::eTransferWrite,
        .dstAccessMask = vk::AccessFlagBits::eTransferRead,
        .oldLayout = vk::ImageLayout::eTransferDstOptimal,
        .newLayout = vk::ImageLayout::eTransferSrcOptimal,
        .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
        .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
        .image = image
    };

    int32_t mipWidth = texWidth;
    int32_t mipHeight = texHeight;

    for (uint32_t i {1}; i < mipLevels; ++i) {
        barrier.subresourceRange.baseMipLevel = i - 1;
        barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
        barrier.newLayout = vk::ImageLayout::eTransferSrcOptimal;
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;

        commandBuffer->pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer, {}, {}, {}, barrier);

        vk::ArrayWrapper1D<vk::Offset3D, 2> offsets, dstOffsets;
        offsets[0] = vk::Offset3D(0, 0, 0);
        offsets[1] = vk::Offset3D(mipWidth, mipHeight, 1);
        dstOffsets[0] = vk::Offset3D(0, 0, 0);
        dstOffsets[1] = vk::Offset3D(mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1);
        vk::ImageBlit blit = {.srcSubresource = {}, .srcOffsets = offsets, .dstSubresource = {}, .dstOffsets = dstOffsets};
        blit.srcSubresource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, i - 1, 0, 1);
        blit.dstSubresource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, i, 0, 1);

        commandBuffer->blitImage(image, vk::ImageLayout::eTransferSrcOptimal, image, vk::ImageLayout::eTransferDstOptimal, {blit}, vk::Filter::eLinear);

        barrier.oldLayout = vk::ImageLayout::eTransferSrcOptimal;
        barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferRead;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

        commandBuffer->pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader, {}, {}, {}, barrier);

        if (mipWidth > 1) {mipWidth /= 2;}
        if (mipHeight > 1) {mipHeight /= 2;}
    }
    barrier.subresourceRange.baseMipLevel = mipLevels - 1;
    barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
    barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
    barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

    commandBuffer->pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader, {}, {}, {}, barrier);

    endSingleTimeCommands(*commandBuffer);
}
void VulkanInterface::createTextureImages(const fastgltf::Asset& asset) {
    m_textureImages.clear();
    m_textureImageAllocations.clear();
    m_textureImageViews.clear();
    
    m_textureImages.reserve(asset.images.size());
    m_textureImageAllocations.reserve(asset.images.size());
    m_textureImageViews.reserve(asset.images.size());
    for (const auto& image : asset.images) {
        int texWidth {};
        int texHeight {};
        int texChannels {};
        stbi_uc* pixels;
        
        std::visit(fastgltf::visitor {
            [](...) {},
            [&](const fastgltf::sources::URI& filePath) {
                std::println("Loading image from external file...");
                assert(filePath.fileByteOffset == 0);
                assert(filePath.uri.isLocalPath());
                const std::string path {std::string{SCENE_PATH} + "/" + std::string{filePath.uri.path()}};
                pixels = stbi_load(path.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
            },
            [&](const fastgltf::sources::Vector& vector) {
                std::println("Loading image directly from memory...");
                pixels = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(vector.bytes.data()), static_cast<int>(vector.bytes.size()), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
            },
            [&](const fastgltf::sources::BufferView& bufferViewSource) {
                std::println("Loading image from buffer through a bufferView...");
                auto& bufferView = asset.bufferViews[bufferViewSource.bufferViewIndex];
                auto& buffer = asset.buffers[bufferView.bufferIndex];
                std::visit(fastgltf::visitor {
                    [](auto& arg) {},
                    [&](fastgltf::sources::Vector& vector) {
                        pixels = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(vector.bytes.data() + bufferView.byteOffset), static_cast<int>(bufferView.byteLength), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
                    }
                }, buffer.data);
            }
        }, image.data); // load image
        if (!pixels) {throw std::runtime_error("Failed to load texture image!");}
        mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(texWidth, texHeight)))) + 1;

        vk::raii::Buffer stagingBuffer {nullptr};
        VmaAllocation stagingAllocation {};
        createBuffer(texWidth * texHeight * 4, vk::BufferUsageFlagBits::eTransferSrc, stagingBuffer, stagingAllocation);

        void *stagingData {};
        vmaMapMemory(m_allocator, stagingAllocation, &stagingData);
        memcpy(stagingData, pixels, texWidth * texHeight * 4);
        vmaUnmapMemory(m_allocator, stagingAllocation);

        stbi_image_free(pixels);

        vk::raii::Image imageTemp {nullptr};
        VmaAllocation imageAllocationTemp {nullptr};
        createImage(texWidth, texHeight, mipLevels, vk::Format::eR8G8B8A8Srgb, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled, imageTemp, imageAllocationTemp);

        transitionImageLayout(imageTemp, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferSrcOptimal, mipLevels);
        copyBufferToImage(stagingBuffer, imageTemp, texWidth, texHeight);
        vmaDestroyBuffer(m_allocator, *stagingBuffer, stagingAllocation);

        generateMipmaps(imageTemp, vk::Format::eR8G8B8A8Srgb, texWidth, texHeight, mipLevels);

        auto imageViewTemp {createImageView(imageTemp, vk::Format::eR8G8B8A8Srgb, vk::ImageAspectFlagBits::eColor, mipLevels)};

        m_textureImages.push_back(std::move(imageTemp));
        m_textureImageAllocations.push_back(std::move(imageAllocationTemp));
        m_textureImageViews.push_back(std::move(imageViewTemp));
    }
}
void VulkanInterface::createTextureSamplers(const fastgltf::Asset& asset) {
    m_textureSamplers.clear();
    m_textureSamplers.reserve(asset.samplers.size());
    for (const auto& sampler : asset.samplers) {
        vk::PhysicalDeviceProperties properties = m_physicalDevice.getProperties();

        assert(sampler.magFilter.has_value());
        vk::Filter magFilter {};
        switch(sampler.magFilter.value()) {
            case fastgltf::Filter::Linear:
                magFilter = vk::Filter::eLinear;
                break;
            default:
                magFilter = vk::Filter::eNearest;
        }
        assert(sampler.minFilter.has_value());
        vk::Filter minFilter {};
        vk::SamplerMipmapMode mipmapMode {};
        switch(sampler.magFilter.value()) {
            case fastgltf::Filter::Linear:
            case fastgltf::Filter::LinearMipMapLinear:
                minFilter = vk::Filter::eLinear;
                mipmapMode = vk::SamplerMipmapMode::eLinear;
                break;
            case fastgltf::Filter::Nearest:
            case fastgltf::Filter::NearestMipMapLinear:
                minFilter = vk::Filter::eNearest;
                mipmapMode = vk::SamplerMipmapMode::eLinear;
                break;
            case fastgltf::Filter::LinearMipMapNearest:
                minFilter = vk::Filter::eLinear;
                mipmapMode = vk::SamplerMipmapMode::eNearest;
                break;
            case fastgltf::Filter::NearestMipMapNearest:
                minFilter = vk::Filter::eNearest;
                mipmapMode = vk::SamplerMipmapMode::eNearest;
                break;
        }
        vk::SamplerAddressMode addressModeU {};
        switch(sampler.wrapS) {
            case fastgltf::Wrap::ClampToEdge:
                addressModeU = vk::SamplerAddressMode::eClampToEdge;
                break;
            case fastgltf::Wrap::MirroredRepeat:
                addressModeU = vk::SamplerAddressMode::eMirroredRepeat;
                break;
            case fastgltf::Wrap::Repeat:
                addressModeU = vk::SamplerAddressMode::eRepeat;
                break;
        }
        vk::SamplerAddressMode addressModeV {};
        switch(sampler.wrapT) {
            case fastgltf::Wrap::ClampToEdge:
                addressModeV = vk::SamplerAddressMode::eClampToEdge;
                break;
            case fastgltf::Wrap::MirroredRepeat:
                addressModeV = vk::SamplerAddressMode::eMirroredRepeat;
                break;
            case fastgltf::Wrap::Repeat:
                addressModeV = vk::SamplerAddressMode::eRepeat;
                break;
        }
		vk::SamplerCreateInfo samplerInfo {
            .magFilter = magFilter,
            .minFilter = minFilter,
            .mipmapMode = mipmapMode,
            .addressModeU = addressModeU,
            .addressModeV = addressModeV,
            .addressModeW = vk::SamplerAddressMode::eRepeat,
            .mipLodBias = 0.0f,
            .anisotropyEnable = vk::True,
            .maxAnisotropy = properties.limits.maxSamplerAnisotropy,
            .compareEnable = vk::False,
            .compareOp = vk::CompareOp::eAlways,
            .minLod = 0.0f,
            .maxLod = vk::LodClampNone
        };
		m_textureSamplers.push_back(std::move(vk::raii::Sampler(m_device, samplerInfo)));
    }
}

void VulkanInterface::loadModels(const fastgltf::Asset& asset) {
    for (const auto& mesh : asset.meshes) {
        
    }
}
