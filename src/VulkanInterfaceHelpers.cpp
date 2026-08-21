#include <fstream>

#include "VulkanInterface.hpp"

#include "Scene.hpp"

#include "PRNG.h"

#include "mikktspace.h"

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
		   supportsRequiredFeatures and
		   physicalDevice.getProperties().deviceType == vk::PhysicalDeviceType::eDiscreteGpu;
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
	vk::Image& image,
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
	image = imageTemp;
}

void VulkanInterface::transitionImageLayout(
	const vk::Image& image,
	const vk::ImageLayout oldLayout,
	const vk::ImageLayout newLayout,
	uint32_t mipLevels
) {
	const auto commandBuffer = beginSingleTimeCommands();

	vk::ImageMemoryBarrier2 barrier {
		.srcStageMask = vk::PipelineStageFlagBits2::eNone,
		.dstStageMask = vk::PipelineStageFlagBits2::eAllCommands,
		.oldLayout = oldLayout,
		.newLayout = newLayout,
		.image = image,
		.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, mipLevels, 0, 1}
	};
	vk::DependencyInfo dependencyInfo {
		.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier
	};
	commandBuffer->pipelineBarrier2(dependencyInfo);
	endSingleTimeCommands(*commandBuffer);
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
	vk::Buffer& buffer,
	VmaAllocation& allocation,
	vk::DeviceSize minAlignment
) const {
	VkBufferCreateInfo bufferInfo {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
	bufferInfo.size = size;
	bufferInfo.usage = static_cast<VkBufferUsageFlags>(usage);

	VmaAllocationCreateInfo allocInfo {};
	allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
	allocInfo.flags = allocFlags;
	allocInfo.minAlignment = minAlignment;

	VkBuffer bufferTemp;
	if (vmaCreateBuffer(m_allocator, &bufferInfo, &allocInfo, &bufferTemp, &allocation, nullptr) !=
		VK_SUCCESS) {
		throw std::runtime_error("Failed to create buffer!");
	}
	buffer = bufferTemp;
}
void VulkanInterface::createGPUBufferWithData(
	vk::DeviceSize bufferSize,
	vk::BufferUsageFlags usageFlags,
	const void* data,
	vk::Buffer& buffer,
	VmaAllocation& allocation,
	vk::DeviceSize minAlignment,
	vk::DeviceSize dataSize
) const {
	vk::Buffer stagingBuffer {nullptr};
	VmaAllocation stagingAllocation {};
	createBuffer(
		bufferSize,
		vk::BufferUsageFlagBits::eTransferSrc,
		VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
		stagingBuffer,
		stagingAllocation
	);
	vmaCopyMemoryToAllocation(
		m_allocator, data, stagingAllocation, 0, dataSize == 0 ? bufferSize : dataSize
	);
	createBuffer(
		bufferSize,
		usageFlags | vk::BufferUsageFlagBits::eTransferDst,
		{},
		buffer,
		allocation,
		minAlignment
	);
	copyBuffer(stagingBuffer, buffer, bufferSize);
	vmaDestroyBuffer(m_allocator, stagingBuffer, stagingAllocation);
}
void VulkanInterface::createHostBufferWithData(
	vk::DeviceSize bufferSize,
	vk::BufferUsageFlags usageFlags,
	const void* data,
	vk::Buffer& buffer,
	VmaAllocation& allocation,
	vk::DeviceSize minAlignment,
	vk::DeviceSize dataSize
) const {
	createBuffer(
		bufferSize,
		usageFlags,
		VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
		buffer,
		allocation,
		minAlignment
	);
	vmaCopyMemoryToAllocation(
		m_allocator, data, allocation, 0, dataSize == 0 ? bufferSize : dataSize
	);
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

void VulkanInterface::cleanup() {
	m_textureImages.clear();
	vmaDestroyImage(m_allocator, m_depthImage, m_depthImageAllocation);
	vmaDestroyBuffer(m_allocator, m_materialBuffer, m_materialAllocation);
	vmaDestroyBuffer(m_allocator, m_vertexBuffer, m_vertexAllocation);
	vmaDestroyBuffer(m_allocator, m_indexBuffer, m_indexAllocation);
	for (int frameIndex {0}; frameIndex < MAX_FRAMES_IN_FLIGHT; ++frameIndex) {
		vmaDestroyBuffer(
			m_allocator,
			m_modelTransformBuffers[frameIndex],
			m_modelTransformAllocations[frameIndex]
		);
		vmaDestroyBuffer(
			m_allocator, m_vpTransformBuffers[frameIndex], m_vpTransformAllocations[frameIndex]
		);
		vmaDestroyBuffer(m_allocator, m_lightBuffers[frameIndex], m_lightAllocations[frameIndex]);
		vmaDestroyBuffer(
			m_allocator, m_drawCommandsBuffers[frameIndex], m_drawCommandsAllocations[frameIndex]
		);
		vmaDestroyBuffer(
			m_allocator, m_metadataBuffers[frameIndex], m_metadataAllocations[frameIndex]
		);
		vmaDestroyBuffer(
			m_allocator,
			m_ddgiProbeSampleBuffers[frameIndex],
			m_ddgiProbeSampleAllocations[frameIndex]
		);
		vmaDestroyBuffer(
			m_allocator,
			m_ddgiProbeBoundsBuffers[frameIndex],
			m_ddgiProbeBoundsAllocations[frameIndex]
		);
		vmaDestroyBuffer(
			m_allocator,
			m_ddgiProbePositionBuffers[frameIndex],
			m_ddgiProbePositionAllocations[frameIndex]
		);
		vmaDestroyBuffer(
			m_allocator,
			m_ddgiClearIndexBuffers[frameIndex],
			m_ddgiClearIndexAllocations[frameIndex]
		);
	}
	vmaDestroyImage(m_allocator, m_ddgiIrradianceImage, m_ddgiIrradianceAllocation);
	vmaDestroyImage(m_allocator, m_ddgiDepthImage, m_ddgiDepthAllocation);
	vmaDestroyImage(
		m_allocator, m_ddgiRadianceTransmissionImage, m_ddgiRadianceTransmissionAllocation
	);
	vmaDestroyImage(
		m_allocator, m_ddgiDistanceTransmissionImage, m_ddgiDistanceTransmissionAllocation
	);
	vmaDestroyImage(m_allocator, m_ddgiDepthSampleCountImage, m_ddgiDepthSampleCountAllocation);
	for (int blasIndex {0}; blasIndex < m_blasBuffers.size(); ++blasIndex) {
		vmaDestroyBuffer(m_allocator, m_blasBuffers[blasIndex], m_blasAllocations[blasIndex]);
	}
	vmaDestroyBuffer(m_allocator, m_blasInstanceBuffer, m_blasInstanceAllocation);
	vmaDestroyBuffer(m_allocator, m_tlasBuffer, m_tlasAllocation);
	vmaDestroyBuffer(m_allocator, m_tlasScratchBuffer, m_tlasScratchAllocation);
	vmaDestroyBuffer(m_allocator, m_tlasLutBuffer, m_tlasLutAllocation);
	vmaDestroyAllocator(m_allocator);
	glfwDestroyWindow(m_window);
	glfwTerminate();
}

// loadScene helper functions
void VulkanInterface::copyBuffer(
	vk::Buffer& srcBuffer, vk::Buffer& dstBuffer, vk::DeviceSize size
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
	commandCopyBuffer.copyBuffer(srcBuffer, dstBuffer, vk::BufferCopy {.size = size});
	commandCopyBuffer.end();
	m_queue.submit(
		vk::SubmitInfo {.commandBufferCount = 1, .pCommandBuffers = &*commandCopyBuffer}, nullptr
	);
	m_queue.waitIdle();
}

vk::DescriptorPoolCreateInfo VulkanInterface::createDescriptorPool(uint32_t textureCount) const {
	std::array<vk::DescriptorPoolSize, 20> poolSize {
		{{.type = vk::DescriptorType::eStorageBuffer, .descriptorCount = MAX_FRAMES_IN_FLIGHT},
		 {.type = vk::DescriptorType::eUniformBuffer, .descriptorCount = MAX_FRAMES_IN_FLIGHT},
		 {.type = vk::DescriptorType::eStorageBuffer, .descriptorCount = MAX_FRAMES_IN_FLIGHT},
		 {.type = vk::DescriptorType::eCombinedImageSampler,
		  .descriptorCount = textureCount * MAX_FRAMES_IN_FLIGHT},
		 {.type = vk::DescriptorType::eStorageBuffer, .descriptorCount = MAX_FRAMES_IN_FLIGHT},
		 {.type = vk::DescriptorType::eAccelerationStructureKHR,
		  .descriptorCount = MAX_FRAMES_IN_FLIGHT},
		 {.type = vk::DescriptorType::eStorageBuffer, .descriptorCount = MAX_FRAMES_IN_FLIGHT},
		 {.type = vk::DescriptorType::eStorageBuffer, .descriptorCount = MAX_FRAMES_IN_FLIGHT},
		 {.type = vk::DescriptorType::eStorageBuffer, .descriptorCount = MAX_FRAMES_IN_FLIGHT},
		 {.type = vk::DescriptorType::eStorageBuffer, .descriptorCount = MAX_FRAMES_IN_FLIGHT},
		 {.type = vk::DescriptorType::eStorageBuffer, .descriptorCount = MAX_FRAMES_IN_FLIGHT},
		 {.type = vk::DescriptorType::eStorageImage, .descriptorCount = MAX_FRAMES_IN_FLIGHT},
		 {.type = vk::DescriptorType::eStorageImage, .descriptorCount = MAX_FRAMES_IN_FLIGHT},
		 {.type = vk::DescriptorType::eStorageBuffer, .descriptorCount = MAX_FRAMES_IN_FLIGHT},
		 {.type = vk::DescriptorType::eUniformBuffer, .descriptorCount = MAX_FRAMES_IN_FLIGHT},
		 {.type = vk::DescriptorType::eStorageImage, .descriptorCount = MAX_FRAMES_IN_FLIGHT},
		 {.type = vk::DescriptorType::eStorageImage, .descriptorCount = MAX_FRAMES_IN_FLIGHT},
		 {.type = vk::DescriptorType::eCombinedImageSampler,
		  .descriptorCount = MAX_FRAMES_IN_FLIGHT},
		 {.type = vk::DescriptorType::eCombinedImageSampler,
		  .descriptorCount = MAX_FRAMES_IN_FLIGHT},
		 {.type = vk::DescriptorType::eStorageImage, .descriptorCount = MAX_FRAMES_IN_FLIGHT}}
	};
	vk::DescriptorPoolCreateInfo poolInfo {
		.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
		.maxSets = MAX_FRAMES_IN_FLIGHT,
		.poolSizeCount = static_cast<uint32_t>(poolSize.size()),
		.pPoolSizes = poolSize.data()
	};
	return poolInfo;
}
vk::raii::DescriptorSetLayout
VulkanInterface::createDescriptorSetLayout(uint32_t textureCount) const {
	std::array<vk::DescriptorSetLayoutBinding, 20> bindings {
		{{0, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eVertex, nullptr},
		 {.binding = 1,
		  .descriptorType = vk::DescriptorType::eUniformBuffer,
		  .descriptorCount = 1,
		  .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment |
						vk::ShaderStageFlagBits::eCompute,
		  .pImmutableSamplers = nullptr},
		 {.binding = 2,
		  .descriptorType = vk::DescriptorType::eStorageBuffer,
		  .descriptorCount = 1,
		  .stageFlags = vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eCompute,
		  .pImmutableSamplers = nullptr},
		 {.binding = 3,
		  .descriptorType = vk::DescriptorType::eCombinedImageSampler,
		  .descriptorCount = textureCount,
		  .stageFlags = vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eCompute,
		  .pImmutableSamplers = nullptr},
		 {.binding = 4,
		  .descriptorType = vk::DescriptorType::eStorageBuffer,
		  .descriptorCount = 1,
		  .stageFlags = vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eCompute,
		  .pImmutableSamplers = nullptr},
		 {.binding = 5,
		  .descriptorType = vk::DescriptorType::eAccelerationStructureKHR,
		  .descriptorCount = 1,
		  .stageFlags = vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eCompute,
		  .pImmutableSamplers = nullptr},
		 {.binding = 6,
		  .descriptorType = vk::DescriptorType::eStorageBuffer,
		  .descriptorCount = 1,
		  .stageFlags = vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eCompute,
		  .pImmutableSamplers = nullptr},
		 {.binding = 7,
		  .descriptorType = vk::DescriptorType::eStorageBuffer,
		  .descriptorCount = 1,
		  .stageFlags = vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eCompute,
		  .pImmutableSamplers = nullptr},
		 {.binding = 8,
		  .descriptorType = vk::DescriptorType::eStorageBuffer,
		  .descriptorCount = 1,
		  .stageFlags = vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eCompute,
		  .pImmutableSamplers = nullptr},
		 {.binding = 9,
		  .descriptorType = vk::DescriptorType::eStorageBuffer,
		  .descriptorCount = 1,
		  .stageFlags = vk::ShaderStageFlagBits::eVertex,
		  .pImmutableSamplers = nullptr},
		 {.binding = 10,
		  .descriptorType = vk::DescriptorType::eStorageBuffer,
		  .descriptorCount = 1,
		  .stageFlags = vk::ShaderStageFlagBits::eCompute | vk::ShaderStageFlagBits::eFragment,
		  .pImmutableSamplers = nullptr},
		 {.binding = 11,
		  .descriptorType = vk::DescriptorType::eCombinedImageSampler,
		  .descriptorCount = 1,
		  .stageFlags = vk::ShaderStageFlagBits::eCompute | vk::ShaderStageFlagBits::eFragment,
		  .pImmutableSamplers = nullptr},
		 {.binding = 12,
		  .descriptorType = vk::DescriptorType::eCombinedImageSampler,
		  .descriptorCount = 1,
		  .stageFlags = vk::ShaderStageFlagBits::eCompute | vk::ShaderStageFlagBits::eFragment,
		  .pImmutableSamplers = nullptr},
		 {.binding = 13,
		  .descriptorType = vk::DescriptorType::eStorageBuffer,
		  .descriptorCount = 1,
		  .stageFlags = vk::ShaderStageFlagBits::eCompute,
		  .pImmutableSamplers = nullptr},
		 {.binding = 14,
		  .descriptorType = vk::DescriptorType::eUniformBuffer,
		  .descriptorCount = 1,
		  .stageFlags = vk::ShaderStageFlagBits::eCompute | vk::ShaderStageFlagBits::eFragment,
		  .pImmutableSamplers = nullptr},
		 {.binding = 15,
		  .descriptorType = vk::DescriptorType::eStorageImage,
		  .descriptorCount = 1,
		  .stageFlags = vk::ShaderStageFlagBits::eCompute,
		  .pImmutableSamplers = nullptr},
		 {.binding = 16,
		  .descriptorType = vk::DescriptorType::eStorageImage,
		  .descriptorCount = 1,
		  .stageFlags = vk::ShaderStageFlagBits::eCompute,
		  .pImmutableSamplers = nullptr},
		 {.binding = 17,
		  .descriptorType = vk::DescriptorType::eStorageImage,
		  .descriptorCount = 1,
		  .stageFlags = vk::ShaderStageFlagBits::eCompute,
		  .pImmutableSamplers = nullptr},
		 {.binding = 18,
		  .descriptorType = vk::DescriptorType::eStorageImage,
		  .descriptorCount = 1,
		  .stageFlags = vk::ShaderStageFlagBits::eCompute,
		  .pImmutableSamplers = nullptr},
		 {.binding = 19,
		  .descriptorType = vk::DescriptorType::eStorageImage,
		  .descriptorCount = 1,
		  .stageFlags = vk::ShaderStageFlagBits::eCompute | vk::ShaderStageFlagBits::eFragment,
		  .pImmutableSamplers = nullptr}}
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
void VulkanInterface::clearImage(vk::Image& image, vk::ImageLayout imageLayout) {
	auto commandBuffer {beginSingleTimeCommands()};

	vk::ClearColorValue clearColor {0.0f, 0.0f, 0.0f, 0.0f};
	vk::ImageSubresourceRange subresourceRange {
		.aspectMask = vk::ImageAspectFlagBits::eColor,
		.baseMipLevel = 0,
		.levelCount = 1,
		.baseArrayLayer = 0,
		.layerCount = 1
	};
	commandBuffer->clearColorImage(image, imageLayout, clearColor, subresourceRange);
	endSingleTimeCommands(*commandBuffer);
}
void VulkanInterface::computeTangents(
	const std::vector<glm::vec3>& positions,
	const std::vector<glm::vec3>& normals,
	const std::vector<glm::vec2>& texCoords,
	const std::vector<uint32_t>& indices,
	int indexStart,
	int indexCount,
	int vertexOffset,
	std::vector<glm::vec4>& tangentReturn
) const {
	struct MikkTSpaceData {
		const std::vector<glm::vec3>& positions {};
		const std::vector<glm::vec3>& normals {};
		const std::vector<glm::vec2>& texCoords {};
		const std::vector<uint32_t>& indices {};

		int indexStart {};
		int indexCount {};
		int vertexOffset {};

		std::vector<glm::vec4>& tangentReturn;
	};

	MikkTSpaceData data {
		.positions = positions,
		.normals = normals,
		.texCoords = texCoords,
		.indices = indices,
		.indexStart = indexStart,
		.indexCount = indexCount,
		.vertexOffset = vertexOffset,
		.tangentReturn = tangentReturn
	};

	SMikkTSpaceInterface interface {
		.m_getNumFaces = [](const SMikkTSpaceContext* pContext) -> int {
			const MikkTSpaceData& data = *static_cast<MikkTSpaceData*>(pContext->m_pUserData);
			return data.indexCount / 3;
		},
		.m_getNumVerticesOfFace = [](const SMikkTSpaceContext* pContext, const int iFace) -> int {
			return 3;
		},
		.m_getPosition = [](const SMikkTSpaceContext* pContext,
							float fvPosOut[],
							const int iFace,
							const int iVert) -> void {
			const MikkTSpaceData& data = *static_cast<MikkTSpaceData*>(pContext->m_pUserData);
			int indexOfIndex = data.indexStart + iFace * 3;
			indexOfIndex += iVert;
			int index = data.indices[indexOfIndex];
			glm::vec3 position {data.positions[index + data.vertexOffset]};
			fvPosOut[0] = position.x;
			fvPosOut[1] = position.y;
			fvPosOut[2] = position.z;
		},
		.m_getNormal = [](const SMikkTSpaceContext* pContext,
						  float fvPosOut[],
						  const int iFace,
						  const int iVert) -> void {
			const MikkTSpaceData& data = *static_cast<MikkTSpaceData*>(pContext->m_pUserData);
			int indexOfIndex = data.indexStart + iFace * 3;
			indexOfIndex += iVert;
			int index = data.indices[indexOfIndex];
			glm::vec3 normal {data.normals[index + data.vertexOffset]};
			fvPosOut[0] = normal.x;
			fvPosOut[1] = normal.y;
			fvPosOut[2] = normal.z;
		},
		.m_getTexCoord = [](const SMikkTSpaceContext* pContext,
							float fvPosOut[],
							const int iFace,
							const int iVert) -> void {
			const MikkTSpaceData& data = *static_cast<MikkTSpaceData*>(pContext->m_pUserData);
			int indexOfIndex = data.indexStart + iFace * 3;
			indexOfIndex += iVert;
			int index = data.indices[indexOfIndex];
			glm::vec2 texCoord {data.texCoords[index + data.vertexOffset]};
			fvPosOut[0] = texCoord.x;
			fvPosOut[1] = texCoord.y;
		},
		.m_setTSpace = [](const SMikkTSpaceContext* pContext,
						  const float fvTangent[],
						  const float fvBiTangent[],
						  const float fMagS,
						  const float fMagT,
						  const tbool bIsOrientationPreserving,
						  const int iFace,
						  const int iVert) -> void {
			const MikkTSpaceData& data = *static_cast<MikkTSpaceData*>(pContext->m_pUserData);
			int indexOfIndex = data.indexStart + iFace * 3;
			indexOfIndex += iVert;
			int index = data.indices[indexOfIndex];
			data.tangentReturn[index + data.vertexOffset].x = fvTangent[0];
			data.tangentReturn[index + data.vertexOffset].y = fvTangent[1];
			data.tangentReturn[index + data.vertexOffset].z = fvTangent[2];
			data.tangentReturn[index + data.vertexOffset].w =
				bIsOrientationPreserving ? 1.0 : (-1.0);
		}
	};
	SMikkTSpaceContext context {
		.m_pInterface = &interface, .m_pUserData = static_cast<void*>(&data)
	};
	genTangSpaceDefault(&context);
} // compute tangents for a single sub mesh

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
void VulkanInterface::transitionImageLayoutPipeline(
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
void VulkanInterface::updateBuffers() {
	DrawCallBufferObject vpTransform {
		m_currentScene->getCameraPosition(),
		lookAt(
			m_currentScene->getCameraPosition(),
			m_currentScene->getCameraPosition() + m_currentScene->getLookAtVector(),
			m_currentScene->getUp()
		),
		glm::perspective(
			glm::radians(45.0f),
			static_cast<float>(m_swapChainExtent.width) /
				static_cast<float>(m_swapChainExtent.height),
			0.1f,
			100.0f
		),
		static_cast<int>(m_currentScene->getLights().size())
	};
	vpTransform.projectionTransform[1][1] *= -1;
	vmaCopyMemoryToAllocation(
		m_allocator, &vpTransform, m_vpTransformAllocations[m_frameIndex], 0, sizeof(vpTransform)
	);

	vmaCopyMemoryToAllocation(
		m_allocator,
		m_currentScene->getLights().data(),
		m_lightAllocations[m_frameIndex],
		0,
		m_currentScene->getLights().size() * sizeof(LightBufferObject)
	);

	const std::vector<ModelTransformBufferObject>& modelTransforms {
		m_currentScene->getModelInstanceTransforms()
	};
	vmaCopyMemoryToAllocation(
		m_allocator,
		modelTransforms.data(),
		m_modelTransformAllocations[m_frameIndex],
		0,
		sizeof(ModelTransformBufferObject) * modelTransforms.size()
	);
	if (m_currentScene->handleCameraMovement(DDGI_PROBE_DIMENSIONS)) {
		restructureDdgiProbes();
		std::vector<DrawIndirectCommand> drawCommands {};
		std::vector<SubMeshMetadataBufferObject> metaData {};
		drawCommands.reserve(m_currentScene->getModelInstancesPerMesh().size() * m_subMeshCount);
		metaData.reserve(m_currentScene->getModelInstancesPerMesh().size() * m_subMeshCount);
		for (const auto& transparentModelInstance: m_currentScene->getTransparentModelInstance()) {
			for (const auto& primitiveIndex: std::get<2>(transparentModelInstance)) {
				Mesh::SubMesh currSubMesh {
					m_meshes[std::get<1>(transparentModelInstance)].subMeshes[primitiveIndex]
				};
				metaData.emplace_back(currSubMesh.materialIndex);

				drawCommands.emplace_back(
					currSubMesh.indexCount,
					1,
					currSubMesh.indexStart,
					currSubMesh.vertexOffset,
					std::get<0>(transparentModelInstance)
				);
			}
		} // draw non-opaque objects
		vmaCopyMemoryToAllocation(
			m_allocator,
			drawCommands.data(),
			m_drawCommandsAllocations[m_frameIndex],
			sizeof(DrawIndirectCommand) * m_opaqueDrawCallsCount,
			sizeof(drawCommands[0]) * drawCommands.size()
		);
		vmaCopyMemoryToAllocation(
			m_allocator,
			metaData.data(),
			m_metadataAllocations[m_frameIndex],
			sizeof(SubMeshMetadataBufferObject) * m_opaqueDrawCallsCount,
			sizeof(metaData[0]) * metaData.size()
		);

		vmaCopyMemoryToAllocation(
			m_allocator,
			m_ddgiProbePositions.data(),
			m_ddgiProbePositionAllocations[m_frameIndex],
			0,
			sizeof(m_ddgiProbePositions[0]) * m_ddgiProbePositions.size()
		);

		if (m_drawDdgiProbes) {
			std::vector<ModelTransformBufferObject> ddgiProbeTransformations {};
			ddgiProbeTransformations.reserve(m_ddgiProbePositions.size());
			uint32_t probeIndex {0};
			for (const auto& position: m_ddgiProbePositions) {
				ddgiProbeTransformations.emplace_back(
					glm::translate(position) * glm::scale(glm::vec3 {0.1, 0.1, 0.1}),
					glm::mat4 {},
					probeIndex
				);
				++probeIndex;
			}
			DrawIndirectCommand probeDrawCommand {
				m_meshes[DDGI_MODEL_INDEX].subMeshes[0].indexCount,
				static_cast<uint32_t>(m_ddgiProbePositions.size()),
				m_meshes[DDGI_MODEL_INDEX].subMeshes[0].indexStart,
				static_cast<int32_t>(m_meshes[DDGI_MODEL_INDEX].subMeshes[0].vertexOffset),
				static_cast<uint32_t>(m_currentScene->getModelInstanceTransforms().size())
			};
			SubMeshMetadataBufferObject probeMetaData {
				m_meshes[DDGI_MODEL_INDEX].subMeshes[0].materialIndex,
				SubMeshMetadataBufferObject::PROBE
			};
			vmaCopyMemoryToAllocation(
				m_allocator,
				ddgiProbeTransformations.data(),
				m_modelTransformAllocations[m_frameIndex],
				sizeof(ModelTransformBufferObject) * modelTransforms.size(),
				sizeof(ModelTransformBufferObject) * m_ddgiProbePositions.size()
			);
			vmaCopyMemoryToAllocation(
				m_allocator,
				&probeDrawCommand,
				m_drawCommandsAllocations[m_frameIndex],
				sizeof(DrawIndirectCommand) *
					(m_opaqueDrawCallsCount + m_transparentDrawCallsCount),
				sizeof(DrawIndirectCommand)
			);
			vmaCopyMemoryToAllocation(
				m_allocator,
				&probeMetaData,
				m_metadataAllocations[m_frameIndex],
				sizeof(SubMeshMetadataBufferObject) *
					(m_opaqueDrawCallsCount + m_transparentDrawCallsCount),
				sizeof(SubMeshMetadataBufferObject)
			);
		}
		vmaCopyMemoryToAllocation(
			m_allocator,
			&(m_currentScene->getDdgiProbeBounds()),
			m_ddgiProbeBoundsAllocations[m_frameIndex],
			0,
			sizeof(DDGIProbeBounds)
		);
		vmaCopyMemoryToAllocation(
			m_allocator,
			m_ddgiClearIndices.data(),
			m_ddgiClearIndexAllocations[m_frameIndex],
			0,
			sizeof(uint32_t) * m_ddgiClearIndices.size()
		);
	}
	vmaCopyMemoryToAllocation(
		m_allocator,
		distributePointsOnUnitSphere(DDGI_PROBE_SAMPLES).data(),
		m_ddgiProbeSampleAllocations[m_frameIndex],
		0,
		sizeof(glm::vec3) * DDGI_PROBE_SAMPLES
	);
}
void VulkanInterface::updateTlas() {
	if (m_currentScene->modelsUpdated()) {
		for (
			int modelInstanceIndex {0};
			modelInstanceIndex < m_currentScene->getModelInstanceTransforms().size();
			++modelInstanceIndex
		) {
			glm::mat4 currTransform {
				m_currentScene->getModelInstanceTransforms()[modelInstanceIndex].modelTransform
			};
			vk::TransformMatrixKHR transformMatrix {};
			transformMatrix.matrix = std::array<std::array<float, 4>, 3> {
				{{currTransform[0][0],
				  currTransform[1][0],
				  currTransform[2][0],
				  currTransform[3][0]},
				 {currTransform[0][1],
				  currTransform[1][1],
				  currTransform[2][1],
				  currTransform[3][1]},
				 {currTransform[0][2],
				  currTransform[1][2],
				  currTransform[2][2],
				  currTransform[3][2]}}
			};
			m_blasInstances[modelInstanceIndex].setTransform(transformMatrix);
		}

		vk::DeviceSize instanceBufferSize = sizeof(m_blasInstances[0]) * m_blasInstances.size();

		vmaCopyMemoryToAllocation(
			m_allocator, m_blasInstances.data(), m_blasInstanceAllocation, 0, instanceBufferSize
		);

		vk::BufferDeviceAddressInfo instanceAddressInfo {.buffer = m_blasInstanceBuffer};
		vk::DeviceAddress instanceAddress = m_device.getBufferAddressKHR(instanceAddressInfo);

		// Prepare the geometry (instance) data
		auto instancesData = vk::AccelerationStructureGeometryInstancesDataKHR {
			.arrayOfPointers = vk::False, .data = instanceAddress
		};

		vk::AccelerationStructureGeometryDataKHR geometryData(instancesData);

		vk::AccelerationStructureGeometryKHR tlasGeometry {
			.geometryType = vk::GeometryTypeKHR::eInstances, .geometry = geometryData
		};

		// TASK06: Note the new parameters to re-build the TLAS in-place
		vk::AccelerationStructureBuildGeometryInfoKHR tlasBuildGeometryInfo {
			.type = vk::AccelerationStructureTypeKHR::eTopLevel,
			.flags = vk::BuildAccelerationStructureFlagBitsKHR::eAllowUpdate,
			.mode = vk::BuildAccelerationStructureModeKHR::eUpdate,
			.srcAccelerationStructure = m_tlas,
			.dstAccelerationStructure = m_tlas,
			.geometryCount = 1,
			.pGeometries = &tlasGeometry
		};

		vk::BufferDeviceAddressInfo scratchAddressInfo {.buffer = m_tlasScratchBuffer};
		vk::DeviceAddress scratchAddr = m_device.getBufferAddressKHR(scratchAddressInfo);
		tlasBuildGeometryInfo.scratchData.deviceAddress = scratchAddr;

		// Prepare the build range for the TLAS
		vk::AccelerationStructureBuildRangeInfoKHR tlasRangeInfo {
			.primitiveCount = static_cast<uint32_t>(m_blasInstances.size()),
			.primitiveOffset = 0,
			.firstVertex = 0,
			.transformOffset = 0
		};

		// Re-build the TLAS
		auto cmd = beginSingleTimeCommands();

		// Pre-build barrier
		vk::MemoryBarrier preBarrier {
			.srcAccessMask = vk::AccessFlagBits::eAccelerationStructureWriteKHR |
							 vk::AccessFlagBits::eTransferWrite | vk::AccessFlagBits::eShaderRead,
			.dstAccessMask = vk::AccessFlagBits::eAccelerationStructureReadKHR |
							 vk::AccessFlagBits::eAccelerationStructureWriteKHR
		};

		cmd->pipelineBarrier(
			vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR |
				vk::PipelineStageFlagBits::eTransfer | vk::PipelineStageFlagBits::eFragmentShader,
			vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR,
			{},
			preBarrier,
			{},
			{}
		);

		cmd->buildAccelerationStructuresKHR({tlasBuildGeometryInfo}, {&tlasRangeInfo});

		// Post-build barrier
		vk::MemoryBarrier postBarrier {
			.srcAccessMask = vk::AccessFlagBits::eAccelerationStructureWriteKHR,
			.dstAccessMask =
				vk::AccessFlagBits::eAccelerationStructureReadKHR | vk::AccessFlagBits::eShaderRead
		};

		cmd->pipelineBarrier(
			vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR,
			vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR |
				vk::PipelineStageFlagBits::eFragmentShader,
			{},
			postBarrier,
			{},
			{}
		);

		endSingleTimeCommands(*cmd);

		m_currentScene->finishHandlingModelUpdates();
	}
}
void VulkanInterface::restructureDdgiProbes() {
	m_ddgiClearIndices.clear();
	const DDGIProbeBounds& bounds {m_currentScene->getDdgiProbeBounds()};
	for (int x {bounds.xLower}; x <= bounds.xUpper; ++x) {
		for (int y {bounds.yLower}; y <= bounds.yUpper; ++y) {
			for (int z {bounds.zLower}; z <= bounds.zUpper; ++z) {
				size_t index {probeCoordinatesToIndex(x, y, z)};
				if (m_ddgiProbePositions[index] != glm::vec3 {x, y, z}) {
					m_ddgiProbePositions[index] = glm::vec3 {x, y, z};
					m_ddgiClearIndices.push_back(index);
				}
			}
		}
	}
}
size_t VulkanInterface::probeCoordinatesToIndex(int x, int y, int z) const {
	const glm::vec3& cameraPosition {m_currentScene->getCameraPosition()};
	int newX {(x + DDGI_PROBE_DIMENSIONS.x / 2 - 1) % DDGI_PROBE_DIMENSIONS.x};
	if (newX < 0) {
		newX += DDGI_PROBE_DIMENSIONS.x;
	}
	int newY {(y + DDGI_PROBE_DIMENSIONS.y / 2 - 1) % DDGI_PROBE_DIMENSIONS.y};
	if (newY < 0) {
		newY += DDGI_PROBE_DIMENSIONS.y;
	}
	int newZ {(z + DDGI_PROBE_DIMENSIONS.z / 2 - 1) % DDGI_PROBE_DIMENSIONS.z};
	if (newZ < 0) {
		newZ += DDGI_PROBE_DIMENSIONS.z;
	}
	return newX * DDGI_PROBE_DIMENSIONS.y * DDGI_PROBE_DIMENSIONS.z +
		   newY * DDGI_PROBE_DIMENSIONS.z + newZ;
}

std::vector<glm::vec3> VulkanInterface::distributePointsOnUnitSphere(int samples) const {
	PRNG rng {};
	float rotationAngle {static_cast<float>(rng.getRandomFloat() * 2 * std::numbers::pi)};

	std::vector<glm::vec3> points {};
	float phi {static_cast<float>((3 - std::sqrt(5)) * std::numbers::pi)};
	points.reserve(samples);
	for (int sample {0}; sample < samples; ++sample) {
		float z {1 - 2 * (static_cast<float>(sample) / samples)};
		float radius {static_cast<float>(sqrt(1 - z * z))};

		float theta {phi * sample};

		float x = cos(theta) * radius;
		float y = sin(theta) * radius;
		points.emplace_back(x, y, z);
	}
	return points;
}
