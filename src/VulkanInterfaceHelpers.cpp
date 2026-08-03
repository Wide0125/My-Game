#include <fstream>
#include <print>

#include "VulkanInterface.hpp"

#include "Scene.hpp"

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
	vk::DeviceSize minAlignment
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
	vmaCopyMemoryToAllocation(m_allocator, data, stagingAllocation, 0, bufferSize);
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
	vk::DeviceSize minAlignment
) const {
	createBuffer(
		bufferSize,
		usageFlags,
		VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
		buffer,
		allocation,
		minAlignment
	);
	vmaCopyMemoryToAllocation(m_allocator, data, allocation, 0, bufferSize);
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
	std::array<vk::DescriptorPoolSize, 10> poolSize {
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
		 {.type = vk::DescriptorType::eStorageBuffer, .descriptorCount = MAX_FRAMES_IN_FLIGHT}}
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
	std::array<vk::DescriptorSetLayoutBinding, 10> bindings {
		{{0, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eVertex, nullptr},
		 {.binding = 1,
		  .descriptorType = vk::DescriptorType::eUniformBuffer,
		  .descriptorCount = 1,
		  .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
		  .pImmutableSamplers = nullptr},
		 {.binding = 2,
		  .descriptorType = vk::DescriptorType::eStorageBuffer,
		  .descriptorCount = 1,
		  .stageFlags = vk::ShaderStageFlagBits::eFragment,
		  .pImmutableSamplers = nullptr},
		 {.binding = 3,
		  .descriptorType = vk::DescriptorType::eCombinedImageSampler,
		  .descriptorCount = textureCount,
		  .stageFlags = vk::ShaderStageFlagBits::eFragment,
		  .pImmutableSamplers = nullptr},
		 {.binding = 4,
		  .descriptorType = vk::DescriptorType::eStorageBuffer,
		  .descriptorCount = 1,
		  .stageFlags = vk::ShaderStageFlagBits::eFragment,
		  .pImmutableSamplers = nullptr},
		 {.binding = 5,
		  .descriptorType = vk::DescriptorType::eAccelerationStructureKHR,
		  .descriptorCount = 1,
		  .stageFlags = vk::ShaderStageFlagBits::eFragment,
		  .pImmutableSamplers = nullptr},
		 {.binding = 6,
		  .descriptorType = vk::DescriptorType::eStorageBuffer,
		  .descriptorCount = 1,
		  .stageFlags = vk::ShaderStageFlagBits::eFragment,
		  .pImmutableSamplers = nullptr},
		 {.binding = 7,
		  .descriptorType = vk::DescriptorType::eStorageBuffer,
		  .descriptorCount = 1,
		  .stageFlags = vk::ShaderStageFlagBits::eFragment,
		  .pImmutableSamplers = nullptr},
		 {.binding = 8,
		  .descriptorType = vk::DescriptorType::eStorageBuffer,
		  .descriptorCount = 1,
		  .stageFlags = vk::ShaderStageFlagBits::eFragment,
		  .pImmutableSamplers = nullptr},
		 {.binding = 9,
		  .descriptorType = vk::DescriptorType::eStorageBuffer,
		  .descriptorCount = 1,
		  .stageFlags = vk::ShaderStageFlagBits::eVertex,
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
void VulkanInterface::updateBuffers() {
	VPTransformBufferObject vpTransform {
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
		std::vector<ModelTransformBufferObject> ddgiTransforms {};
		ddgiTransforms.reserve(m_ddgiProbePositions.size());
		for (const auto& position: m_ddgiProbePositions) {
			ddgiTransforms.emplace_back(
				glm::translate(position) * glm::scale(glm::vec3 {0.05, 0.05, 0.05})
			);
		}
		vmaCopyMemoryToAllocation(
			m_allocator,
			ddgiTransforms.data(),
			m_ddgiTransformAllocations[m_frameIndex],
			0,
			sizeof(ddgiTransforms[0]) * ddgiTransforms.size()
		);
	}
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

	const std::array<std::pair<int, int>, 3>& bounds {m_currentScene->getDdgiProbeBounds()};
	for (int x {bounds[0].first}; x <= bounds[0].second; ++x) {
		for (int y {bounds[1].first}; y <= bounds[1].second; ++y) {
			for (int z {bounds[2].first}; z <= bounds[2].second; ++z) {
				size_t index {probeCoordinatesToIndex(x, y, z)};
				if (m_ddgiProbePositions[index] != glm::vec3 {x, y, z}) {
					m_ddgiProbePositions[index] = glm::vec3 {x, y, z};
				}
			}
		}
	}
}
size_t VulkanInterface::probeCoordinatesToIndex(int x, int y, int z) const {
	const glm::vec3& cameraPosition {m_currentScene->getCameraPosition()};
	int newX {(x + std::get<0>(DDGI_PROBE_DIMENSIONS) / 2) % std::get<0>(DDGI_PROBE_DIMENSIONS)};
	if (newX < 0) {
		newX += std::get<0>(DDGI_PROBE_DIMENSIONS);
	}
	int newY {(y + std::get<1>(DDGI_PROBE_DIMENSIONS) / 2) % std::get<1>(DDGI_PROBE_DIMENSIONS)};
	if (newY < 0) {
		newY += std::get<1>(DDGI_PROBE_DIMENSIONS);
	}
	int newZ {(z + std::get<2>(DDGI_PROBE_DIMENSIONS) / 2) % std::get<2>(DDGI_PROBE_DIMENSIONS)};
	if (newZ < 0) {
		newZ += std::get<2>(DDGI_PROBE_DIMENSIONS);
	}
	return newX * std::get<1>(DDGI_PROBE_DIMENSIONS) * std::get<2>(DDGI_PROBE_DIMENSIONS) +
		   newY * std::get<2>(DDGI_PROBE_DIMENSIONS) + newZ;
}
