#include <fstream>

#include "VulkanInterface.hpp"

// initVulkan helper functions
bool VulkanInterface::isDeviceSuitable(const vk::raii::PhysicalDevice& physicalDevice) const {
	bool supportsVulkan1_3 {
		physicalDevice.getProperties2().properties.apiVersion >= vk::ApiVersion13
	};

	auto queueFamilies {physicalDevice.getQueueFamilyProperties()};
	bool queueFamilySupportsGraphics {
		std::ranges::any_of(queueFamilies, [](const auto& queueFamily) {
			return static_cast<bool>(queueFamily.queueFlags & vk::QueueFlagBits::eGraphics);
		})
	};
	auto availableDeviceExtensions {physicalDevice.enumerateDeviceExtensionProperties()};

	bool supportsAllExtensions {true};
	for (const auto& requiredDeviceExtension: m_requiredDeviceExtensions) {
		bool supportsExtension {false};
		for (const auto& availableDeviceExtension: availableDeviceExtensions) {
			if (strcmp(requiredDeviceExtension, availableDeviceExtension.extensionName) == 0) {
				supportsExtension = true;
				break;
			}
		}
		if (!supportsExtension) {
			supportsAllExtensions = false;
			break;
		}
	}

	auto features {physicalDevice.template getFeatures2<
		vk::PhysicalDeviceFeatures2,
		vk::PhysicalDeviceVulkan11Features,
		vk::PhysicalDeviceVulkan13Features,
		vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>()};
	bool supportsRequiredFeatures {
		features.template get<vk::PhysicalDeviceFeatures2>().features.samplerAnisotropy and
		features.template get<vk::PhysicalDeviceVulkan11Features>().shaderDrawParameters and
		features.template get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering and
		features.template get<vk::PhysicalDeviceVulkan13Features>().synchronization2 and
		features.template get<vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>()
			.extendedDynamicState
	};

	return supportsVulkan1_3 and queueFamilySupportsGraphics and supportsAllExtensions and
		   supportsRequiredFeatures;
}

vk::Extent2D
VulkanInterface::chooseSwapExtent(const vk::SurfaceCapabilitiesKHR& surfaceCapabilities) const {
	if (surfaceCapabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
		return surfaceCapabilities.currentExtent;
	}
	int width {};
	int height {};
	glfwGetFramebufferSize(m_window, &width, &height);

	return {
		std::clamp<uint32_t>(
			width,
			surfaceCapabilities.minImageExtent.width,
			surfaceCapabilities.maxImageExtent.width
		),
		std::clamp<uint32_t>(
			height,
			surfaceCapabilities.minImageExtent.height,
			surfaceCapabilities.maxImageExtent.height
		)
	};
}
uint32_t
VulkanInterface::chooseSwapMinImageCount(const vk::SurfaceCapabilitiesKHR& surfaceCapabilities) {
	auto minImageCount {std::max(3u, surfaceCapabilities.minImageCount)};
	if ((0 < surfaceCapabilities.maxImageCount) and
		(surfaceCapabilities.maxImageCount < minImageCount)) {
		minImageCount = surfaceCapabilities.maxImageCount;
	}
	return minImageCount;
}
vk::SurfaceFormatKHR VulkanInterface::chooseSwapSurfaceFormat(
	const std::vector<vk::SurfaceFormatKHR>& availableFormats
) {
	assert(!availableFormats.empty());
	const auto formatIt {std::ranges::find_if(availableFormats, [](const auto& availableFormat) {
		return availableFormat.format == vk::Format::eB8G8R8A8Srgb and
			   availableFormat.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear;
	})};
	return formatIt != availableFormats.end() ? *formatIt : availableFormats[0];
}

void VulkanInterface::createImage(
	uint32_t width,
	uint32_t height,
	uint32_t mipLevels,
	vk::Format format,
	vk::ImageTiling tiling,
	vk::ImageUsageFlags usage,
	vk::raii::Image& image,
	VmaAllocation& allocation
) const {
	vk::ImageCreateInfo imageInfo {
		.imageType = vk::ImageType::e2D,
		.format = format,
		.extent = {width, height, 1},
		.mipLevels = mipLevels,
		.arrayLayers = 1,
		.samples = vk::SampleCountFlagBits::e1,
		.tiling = tiling,
		.usage = usage,
		.sharingMode = vk::SharingMode::eExclusive,
		.initialLayout = vk::ImageLayout::eUndefined
	};

	VmaAllocationCreateInfo allocInfo {};
	allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

	VkImage imageTemp {};
	if (vmaCreateImage(m_allocator, &*imageInfo, &allocInfo, &imageTemp, &allocation, nullptr) !=
		VK_SUCCESS) {
		throw std::runtime_error("Image creation failed!");
	}
	image = {m_device, imageTemp};
}
[[nodiscard]] vk::raii::ImageView VulkanInterface::createImageView(
	const vk::Image& image, vk::Format format, vk::ImageAspectFlags aspectFlags, uint32_t mipLevels
) const {
	vk::ImageViewCreateInfo viewInfo {
		.image = image,
		.viewType = vk::ImageViewType::e2D,
		.format = format,
		.subresourceRange = {aspectFlags, 0, mipLevels, 0, 1}
	};
	return vk::raii::ImageView {m_device, viewInfo};
}

void VulkanInterface::createBuffer(
	vk::DeviceSize size,
	vk::BufferUsageFlags usage,
	VmaAllocationCreateFlags allocFlags,
	vk::raii::Buffer& buffer,
	VmaAllocation& allocation
) const {
	VkBufferCreateInfo bufferInfo {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
	bufferInfo.size = size;
	bufferInfo.usage = static_cast<VkBufferUsageFlags>(usage);

	VmaAllocationCreateInfo allocInfo {};
	allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
	allocInfo.flags = allocFlags;

	VkBuffer bufferTemp;
	if (vmaCreateBuffer(m_allocator, &bufferInfo, &allocInfo, &bufferTemp, &allocation, nullptr) !=
		VK_SUCCESS) {
		throw std::runtime_error("Failed to create buffer!");
	}
	buffer = {m_device, bufferTemp};
}

std::unique_ptr<vk::raii::CommandBuffer> VulkanInterface::beginSingleTimeCommands() const {
	vk::CommandBufferAllocateInfo allocInfo {
		.commandPool = m_commandPool,
		.level = vk::CommandBufferLevel::ePrimary,
		.commandBufferCount = 1
	};
	std::unique_ptr<vk::raii::CommandBuffer> commandBuffer =
		std::make_unique<vk::raii::CommandBuffer>(
			std::move(vk::raii::CommandBuffers(m_device, allocInfo).front())
		);

	vk::CommandBufferBeginInfo beginInfo {.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit};
	commandBuffer->begin(beginInfo);

	return commandBuffer;
}
void VulkanInterface::endSingleTimeCommands(const vk::raii::CommandBuffer& commandBuffer) const {
	commandBuffer.end();

	vk::SubmitInfo submitInfo {.commandBufferCount = 1, .pCommandBuffers = &*commandBuffer};
	m_queue.submit(submitInfo, nullptr);
	m_queue.waitIdle();
}

// loadScene helper functions
void VulkanInterface::copyBuffer(
	vk::raii::Buffer& srcBuffer, vk::raii::Buffer& dstBuffer, vk::DeviceSize size
) const {
	vk::CommandBufferAllocateInfo allocInfo {
		.commandPool = m_commandPool,
		.level = vk::CommandBufferLevel::ePrimary,
		.commandBufferCount = 1
	};
	vk::raii::CommandBuffer commandCopyBuffer =
		std::move(m_device.allocateCommandBuffers(allocInfo).front());
	commandCopyBuffer.begin(
		vk::CommandBufferBeginInfo {.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit}
	);
	commandCopyBuffer.copyBuffer(*srcBuffer, *dstBuffer, vk::BufferCopy {.size = size});
	commandCopyBuffer.end();
	m_queue.submit(
		vk::SubmitInfo {.commandBufferCount = 1, .pCommandBuffers = &*commandCopyBuffer}, nullptr
	);
	m_queue.waitIdle();
}

void VulkanInterface::createDescriptorPool(uint32_t textureCount) {
	std::array<vk::DescriptorPoolSize, 5> poolSize {
		{{.type = vk::DescriptorType::eStorageBuffer, .descriptorCount = MAX_FRAMES_IN_FLIGHT},
		 {.type = vk::DescriptorType::eUniformBuffer, .descriptorCount = MAX_FRAMES_IN_FLIGHT},
		 {.type = vk::DescriptorType::eStorageBuffer, .descriptorCount = MAX_FRAMES_IN_FLIGHT},
		 {.type = vk::DescriptorType::eCombinedImageSampler,
		  .descriptorCount = textureCount * MAX_FRAMES_IN_FLIGHT},
		 {.type = vk::DescriptorType::eUniformBuffer, .descriptorCount = MAX_FRAMES_IN_FLIGHT}}
	};
	vk::DescriptorPoolCreateInfo poolInfo {
		.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
		.maxSets = MAX_FRAMES_IN_FLIGHT,
		.poolSizeCount = static_cast<uint32_t>(poolSize.size()),
		.pPoolSizes = poolSize.data()
	};
	m_descriptorPool = {m_device, poolInfo};
}
vk::raii::DescriptorSetLayout
VulkanInterface::createDescriptorSetLayout(uint32_t textureCount) const {
	std::array<vk::DescriptorSetLayoutBinding, 5> bindings {
		{{0, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eVertex, nullptr},
		 {1, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eVertex, nullptr},
		 {2, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eFragment, nullptr},
		 {3,
		  vk::DescriptorType::eCombinedImageSampler,
		  textureCount,
		  vk::ShaderStageFlagBits::eFragment,
		  nullptr},
		 {4, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eFragment, nullptr}}
	};
	vk::DescriptorSetLayoutCreateInfo layoutInfo {
		.bindingCount = static_cast<uint32_t>(bindings.size()), .pBindings = bindings.data()
	};
	return {m_device, layoutInfo};
}

std::vector<char> VulkanInterface::readFile(const std::string& filename) {
	std::ifstream file {filename, std::ios::ate | std::ios::binary};
	if (!file.is_open()) {
		throw std::runtime_error("Failed to open file!");
	}
	std::vector<char> buffer(file.tellg());
	file.seekg(0, std::ios::beg);
	file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
	file.close();
	return buffer;
}
[[nodiscard]] vk::raii::ShaderModule
VulkanInterface::createShaderModule(const std::vector<char>& code) const {
	vk::ShaderModuleCreateInfo createInfo {
		.codeSize = code.size(), .pCode = reinterpret_cast<const uint32_t*>(code.data())
	};
	return {m_device, createInfo};
}
vk::Format VulkanInterface::findSupportedFormat(
	const std::vector<vk::Format>& candidates,
	vk::ImageTiling tiling,
	vk::FormatFeatureFlags features
) const {
	for (const auto format: candidates) {
		vk::FormatProperties props {m_physicalDevice.getFormatProperties(format)};

		if (tiling == vk::ImageTiling::eLinear and
			(props.linearTilingFeatures & features) == features) {
			return format;
		}
		if (tiling == vk::ImageTiling::eOptimal and
			(props.optimalTilingFeatures & features) == features) {
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

// draw helper functions
void VulkanInterface::recreateSwapChain() {
	int width = 0, height = 0;
	glfwGetFramebufferSize(m_window, &width, &height);
	while (width == 0 || height == 0) {
		glfwGetFramebufferSize(m_window, &width, &height);
		glfwWaitEvents();
	}

	m_device.waitIdle();

	m_swapChainImageViews.clear();
	m_swapChain = nullptr;

	createSwapChain();
	createDepthResources();
}
void VulkanInterface::transition_image_layout(
	vk::Image image,
	vk::ImageLayout old_layout,
	vk::ImageLayout new_layout,
	vk::AccessFlags2 src_access_mask,
	vk::AccessFlags2 dst_access_mask,
	vk::PipelineStageFlags2 src_stage_mask,
	vk::PipelineStageFlags2 dst_stage_mask,
	vk::ImageAspectFlags image_aspect_flags
) {
	vk::ImageMemoryBarrier2 barrier = {
		.srcStageMask = src_stage_mask,
		.srcAccessMask = src_access_mask,
		.dstStageMask = dst_stage_mask,
		.dstAccessMask = dst_access_mask,
		.oldLayout = old_layout,
		.newLayout = new_layout,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = image,
		.subresourceRange = {
			.aspectMask = image_aspect_flags,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1
		}
	};
	vk::DependencyInfo dependency_info = {
		.dependencyFlags = {}, .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier
	};
	m_commandBuffers[m_frameIndex].pipelineBarrier2(dependency_info);
}
