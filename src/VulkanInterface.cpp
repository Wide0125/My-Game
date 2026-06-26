#include "VulkanInterface.hpp"
#include "vulkan/vulkan.hpp"

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/hash.hpp>

#include <cstdint>
#include <stdexcept>
#include <print>
#include <algorithm>
#include <array>
#include <fstream>

#include "vertex.hpp"

VulkanInterface::VulkanInterface() {
    initWindow();
    initVulkan();
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

    m_window = glfwCreateWindow(mode->width, mode->height, "My Game", monitor, NULL);
}

void VulkanInterface::initVulkan() {
    createInstance();
    createSurface();
    pickPhysicalDevice();
    createLogicalDevice();
    createSwapChain();
    createSwapChainImageViews();
    createDescriptorSetLayout();
    createGraphicsPipeline();
    createCommandPool();
    createDepthResources();
    createVertexBuffer();
    createIndexBuffer();
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
        {.extendedDynamicState = true} // tvk::PhysicalDeviceExtendedDynamicStateFeaturesEXT
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
void VulkanInterface::createImage(uint32_t width, uint32_t height, uint32_t mipLevels, vk::Format format, vk::ImageTiling tiling, vk::ImageUsageFlags usage, vk::MemoryPropertyFlags properties, vk::raii::Image& image, vk::raii::DeviceMemory& imageMemory) const {
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
        .initialLayout = vk::ImageLayout::eUndefined};
    image = vk::raii::Image(m_device, imageInfo);

    vk::MemoryRequirements memRequirements = image.getMemoryRequirements();
    vk::MemoryAllocateInfo allocInfo{
        .allocationSize  = memRequirements.size,
        .memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties)};
    imageMemory = vk::raii::DeviceMemory(m_device, allocInfo);
    image.bindMemory(imageMemory, 0);
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

    createImage(m_swapChainExtent.width, m_swapChainExtent.height, 1, depthFormat, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eDepthStencilAttachment, vk::MemoryPropertyFlagBits::eDeviceLocal, m_depthImage, m_depthImageMemory);
    m_depthImageView = createImageView(m_depthImage, depthFormat, vk::ImageAspectFlagBits::eDepth, 1);
}

