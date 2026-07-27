#include <algorithm>
#include <cstdint>
#include <print>
#include <stdexcept>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/hash.hpp>

#include <ktx.h>

#include <fastgltf/util.hpp>
#include <string>

#include "VulkanInterface.hpp"

VulkanInterface::VulkanInterface() {
	initWindow();
	initVulkan();
}

void VulkanInterface::initWindow() {
	glfwInit();

	// glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	// glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

	// m_window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan", nullptr, nullptr);
	// glfwSetWindowUserPointer(m_window, this);

	const auto monitor {glfwGetPrimaryMonitor()};
	const GLFWvidmode* mode {glfwGetVideoMode(monitor)};

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
	glfwWindowHint(GLFW_RED_BITS, mode->redBits);
	glfwWindowHint(GLFW_GREEN_BITS, mode->greenBits);
	glfwWindowHint(GLFW_BLUE_BITS, mode->blueBits);
	glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);
	glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, GLFW_TRUE);

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
	createCommandPool();
	createDepthResources();
	createCommandBuffers();
	createSyncObjects();
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
	std::vector<const char*> requiredGLFWExtensions {
		glfwExtensions, glfwExtensions + glfwExtensionCount
	};

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

void VulkanInterface::pickPhysicalDevice() {
	m_availablePhysicalDevices = m_instance.enumeratePhysicalDevices();
	for (const auto& physicalDevice: m_availablePhysicalDevices) {
		std::println(
			"{}", static_cast<std::string>(physicalDevice.getProperties2().properties.deviceName)
		);
	}
	auto const devicesIt {
		std::ranges::find_if(m_availablePhysicalDevices, [&](auto const& physicalDevice) {
			return isDeviceSuitable(physicalDevice);
		})
	};
	if (devicesIt == m_availablePhysicalDevices.end()) {
		throw std::runtime_error("Failed to find a suitable GPU!");
	}
	m_physicalDevice = *devicesIt;
	vk::StructureChain<
		vk::PhysicalDeviceProperties2,
		vk::PhysicalDeviceAccelerationStructurePropertiesKHR>
		physicalDeviceProperties {m_physicalDevice.getProperties2<
			vk::PhysicalDeviceProperties2,
			vk::PhysicalDeviceAccelerationStructurePropertiesKHR>()};
	m_accelerationStructureScratchOffset =
		physicalDeviceProperties.get<vk::PhysicalDeviceAccelerationStructurePropertiesKHR>()
			.minAccelerationStructureScratchOffsetAlignment;
}
void VulkanInterface::createLogicalDevice() {
	std::vector<vk::QueueFamilyProperties> queueFamilies {
		m_physicalDevice.getQueueFamilyProperties()
	};
	for (
		uint32_t queueFamilyIndex = 0; queueFamilyIndex < queueFamilies.size(); ++queueFamilyIndex
	) {
		if ((queueFamilies[queueFamilyIndex].queueFlags & vk::QueueFlagBits::eGraphics) and
			m_physicalDevice.getSurfaceSupportKHR(queueFamilyIndex, *m_surface)) {
			m_queueFamilyIndex = queueFamilyIndex;
			break;
		}
	} // find suitable queue family that supports graphics
	  // and present
	if (m_queueFamilyIndex == ~0) {
		throw std::runtime_error(
			"Could not find a queue for graphics and "
			"present -> terminating"
		);
	}

	// query for Vulkan features
	vk::StructureChain<
		vk::PhysicalDeviceFeatures2,
		vk::PhysicalDeviceVulkan11Features,
		vk::PhysicalDeviceVulkan12Features,
		vk::PhysicalDeviceVulkan13Features,
		vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT,
		vk::PhysicalDeviceAccelerationStructureFeaturesKHR,
		vk::PhysicalDeviceRayQueryFeaturesKHR>
		featureChain {
			{.features =
				 {.multiDrawIndirect = true,
				  .drawIndirectFirstInstance = true,
				  .samplerAnisotropy = true}},
			{.shaderDrawParameters = true},
			{.descriptorIndexing = true,
			 .runtimeDescriptorArray = true,
			 .scalarBlockLayout = true,
			 .bufferDeviceAddress = true},
			{.synchronization2 = true, .dynamicRendering = true},
			{.extendedDynamicState = true},
			{.accelerationStructure = true},
			{.rayQuery = true}
		};

	// create a device
	float queuePriority {0.5};
	vk::DeviceQueueCreateInfo deviceQueueCreateInfo {
		.queueFamilyIndex = m_queueFamilyIndex, .queueCount = 1, .pQueuePriorities = &queuePriority
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
		.flags = VmaAllocatorCreateFlagBits::VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
		.physicalDevice = *m_physicalDevice,
		.device = *m_device,
		.instance = *m_instance,
		.vulkanApiVersion = VK_API_VERSION_1_4
	};
	vmaCreateAllocator(&allocatorCreateInfo, &m_allocator);
}

void VulkanInterface::createSwapChain() {
	vk::SurfaceCapabilitiesKHR surfaceCapabilities {
		m_physicalDevice.getSurfaceCapabilitiesKHR(m_surface)
	};
	m_swapChainExtent = chooseSwapExtent(surfaceCapabilities);
	uint32_t minImageCount {chooseSwapMinImageCount(surfaceCapabilities)};

	std::vector<vk::SurfaceFormatKHR> availableFormats {
		m_physicalDevice.getSurfaceFormatsKHR(m_surface)
	};
	m_swapChainSurfaceFormat = chooseSwapSurfaceFormat(availableFormats);

	std::vector<vk::PresentModeKHR> availablePresentModes {
		m_physicalDevice.getSurfacePresentModesKHR(m_surface)
	};
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

void VulkanInterface::createCommandPool() {
	vk::CommandPoolCreateInfo poolInfo {
		.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
		.queueFamilyIndex = m_queueFamilyIndex
	};
	m_commandPool = {m_device, poolInfo};
}

void VulkanInterface::createDepthResources() {
	vk::Format depthFormat {findDepthFormat()};

	createImage(
		m_swapChainExtent.width,
		m_swapChainExtent.height,
		1,
		depthFormat,
		vk::ImageTiling::eOptimal,
		vk::ImageUsageFlagBits::eDepthStencilAttachment,
		m_depthImage,
		m_depthImageAllocation
	);
	m_depthImageView =
		createImageView(m_depthImage, depthFormat, vk::ImageAspectFlagBits::eDepth, 1);
}

void VulkanInterface::createCommandBuffers() {
	m_commandBuffers.clear();
	vk::CommandBufferAllocateInfo allocInfo {
		.commandPool = m_commandPool,
		.level = vk::CommandBufferLevel::ePrimary,
		.commandBufferCount = MAX_FRAMES_IN_FLIGHT
	};
	m_commandBuffers = vk::raii::CommandBuffers {m_device, allocInfo};
}

void VulkanInterface::createSyncObjects() {
	assert(
		m_presentCompleteSemaphores.empty() and m_renderFinishedSemaphores.empty() and
		m_inFlightFences.empty()
	);
	for (size_t i {0}; i < m_swapChainImages.size(); ++i) {
		m_renderFinishedSemaphores.emplace_back(m_device, vk::SemaphoreCreateInfo());
	}
	for (size_t i {0}; i < MAX_FRAMES_IN_FLIGHT; ++i) {
		m_presentCompleteSemaphores.emplace_back(m_device, vk::SemaphoreCreateInfo());
		m_inFlightFences.emplace_back(
			m_device, vk::FenceCreateInfo {.flags = vk::FenceCreateFlagBits::eSignaled}
		);
	}
}
