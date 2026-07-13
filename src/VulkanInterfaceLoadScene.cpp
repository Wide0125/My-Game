#include <algorithm>
#include <print>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/hash.hpp>

#include <ktx.h>
#include <ktxvulkan.h>

#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>
#include <fastgltf/util.hpp>

#include "Scene.hpp"
#include "VulkanInterface.hpp"

void VulkanInterface::loadScene(const fastgltf::Asset& asset, Scene* scene) {
	m_currentScene = scene;
	createTextureImages(asset);
	createTextureSamplers(asset);
	loadMeshes(asset);
	loadMaterials(asset);
	createBuffers(asset);
	createDescriptorSets(asset);
	createGraphicsPipeline();
}
void VulkanInterface::createTextureImages(const fastgltf::Asset& asset) {
	m_textureImages.clear();
	m_textureImageMemories.clear();
	m_textureImageViews.clear();

	m_textureImages.reserve(asset.images.size());
	m_textureImageMemories.reserve(asset.images.size());
	m_textureImageViews.reserve(asset.images.size());
	for (const auto& image: asset.images) {

		ktxTexture2* kTexture {};
		KTX_error_code result {KTX_FILE_OPEN_FAILED};

		std::visit(
			fastgltf::visitor {
				[](auto& a) {},
				[&](const fastgltf::sources::URI& filePath) {
					std::println(
						"Loading image from external "
						"file..."
					);
					assert(filePath.fileByteOffset == 0);
					assert(filePath.uri.isLocalPath());
					assert(
						filePath.mimeType == fastgltf::MimeType::KTX2 and "Texture is not KTX2!"
					);
					const std::string path {
						std::string {SCENE_PATH} + "/" + std::string {filePath.uri.path()}
					};
					result = ktxTexture2_CreateFromNamedFile(
						path.c_str(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &kTexture
					);
				},
				[&](const fastgltf::sources::Vector& vector) {
					std::println(
						"Loading image directly from "
						"memory..."
					);
					assert(vector.mimeType == fastgltf::MimeType::KTX2 and "Texture is not KTX2!");
					result = ktxTexture2_CreateFromMemory(
						reinterpret_cast<const ktx_uint8_t*>(vector.bytes.data()),
						static_cast<ktx_size_t>(vector.bytes.size()),
						KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT,
						&kTexture
					);
				},
				[&](const fastgltf::sources::BufferView& bufferViewSource) {
					std::println(
						"Loading image from buffer through "
						"a bufferView..."
					);
					assert(
						bufferViewSource.mimeType == fastgltf::MimeType::KTX2 and
						"Texture is not KTX2!"
					);
					auto& bufferView = asset.bufferViews[bufferViewSource.bufferViewIndex];
					auto& buffer = asset.buffers[bufferView.bufferIndex];
					std::visit(
						fastgltf::visitor {
							[](auto& arg) {},
							[&](const fastgltf::sources::Array& array) {
								result = ktxTexture2_CreateFromMemory(
									reinterpret_cast<const ktx_uint8_t*>(
										array.bytes.data() + bufferView.byteOffset
									),
									static_cast<ktx_size_t>(bufferView.byteLength),
									KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT,
									&kTexture
								);
							}
						},
						buffer.data
					);
				}
			},
			image.data
		); // load image into kTexture
		if (result != KTX_SUCCESS) {
			throw std::runtime_error("Failed to load texture image!");
		}
		if (kTexture->isCompressed and ktxTexture2_NeedsTranscoding(kTexture)) {
			ktx_transcode_fmt_e transcodeFmt = KTX_TTF_BC7_RGBA;

			result = ktxTexture2_TranscodeBasis(kTexture, transcodeFmt, 0);
			if (result != KTX_SUCCESS) {
				throw std::runtime_error(
					std::format("Failed to transcode KTX2 texture: {}", ktxErrorString(result))
				);
			}
		}
		ktxVulkanTexture vkTexture {};
		ktxVulkanDeviceInfo* ktxVkDeviceInfo {
			ktxVulkanDeviceInfo_Create(*m_physicalDevice, *m_device, *m_queue, *m_commandPool, NULL)
		};
		result = ktxTexture2_VkUpload(kTexture, ktxVkDeviceInfo, &vkTexture);
		if (result != KTX_SUCCESS) {
			throw std::runtime_error("Failed to upload texture!");
		}
		ktxTexture2_Destroy(kTexture);

		m_textureImages.emplace_back(m_device, vkTexture.image);
		m_textureImageMemories.emplace_back(m_device, vkTexture.deviceMemory);
		m_textureImageViews.push_back(
			std::move(createImageView(
				vkTexture.image,
				static_cast<vk::Format>(vkTexture.imageFormat),
				vk::ImageAspectFlagBits::eColor,
				vkTexture.levelCount
			))
		);
	}
	for (const auto& texture: asset.textures) {
	}
}
void VulkanInterface::createTextureSamplers(const fastgltf::Asset& asset) {
	m_textureSamplers.clear();
	m_textureSamplers.reserve(asset.samplers.size());
	for (const auto& sampler: asset.samplers) {
		vk::PhysicalDeviceProperties properties = m_physicalDevice.getProperties2().properties;

		assert(sampler.magFilter.has_value());
		vk::Filter magFilter {};
		switch (sampler.magFilter.value()) {
			case fastgltf::Filter::Linear:
				magFilter = vk::Filter::eLinear;
				break;
			default:
				magFilter = vk::Filter::eNearest;
		}
		assert(sampler.minFilter.has_value());
		vk::Filter minFilter {};
		vk::SamplerMipmapMode mipmapMode {};
		switch (sampler.magFilter.value()) {
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
		switch (sampler.wrapS) {
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
		switch (sampler.wrapT) {
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

void VulkanInterface::loadMeshes(const fastgltf::Asset& asset) {
	destroyMeshes();
	m_meshes.reserve(asset.meshes.size());
	for (const auto& mesh: asset.meshes) {
		size_t positionsCount {0};
		size_t hasIndicesCount {0};
		size_t hasMaterialCount {0};
		for (const auto& primitive: mesh.primitives) {
			positionsCount +=
				asset.accessors[primitive.findAttribute("POSITION")->accessorIndex].count;
			hasIndicesCount += primitive.indicesAccessor.has_value() ? 1 : 0;
		}
		std::vector<glm::vec3> positions(positionsCount);
		std::vector<glm::vec3> normals(positionsCount);
		std::vector<glm::vec3> colors(positionsCount);
		std::vector<glm::vec2> texCoords(positionsCount);

		std::vector<vk::raii::Buffer> indexBuffers {};
		std::vector<VmaAllocation> indexBufferAllocations {};
		uint32_t indicesCount {0};
		indexBuffers.reserve(hasIndicesCount);
		indexBufferAllocations.reserve(hasIndicesCount);
		std::vector<uint32_t> materialIndices {};
		materialIndices.reserve(hasIndicesCount);
		size_t offset {0};
		for (const auto& primitive: mesh.primitives) {
			const auto& positionAccessor { // load vertices
				asset.accessors[primitive.findAttribute("POSITION")->accessorIndex]
			};
			fastgltf::copyFromAccessor<glm::vec3>(
				asset, positionAccessor, positions.data() + offset
			);
			const auto normalIt {primitive.findAttribute("NORMAL")}; // load normals
			if (normalIt != primitive.attributes.end()) {
				const auto& normalsAccessor {asset.accessors[normalIt->accessorIndex]};
				fastgltf::copyFromAccessor<glm::vec3>(
					asset, normalsAccessor, normals.data() + offset
				);
			} else {
				std::fill(
					normals.begin() + offset,
					normals.begin() + offset + positionAccessor.count,
					glm::vec3 {0, 0, 0}
				);
			}
			const auto colorIt {primitive.findAttribute("COLOR_0")}; // load colors
			if (colorIt != primitive.attributes.end()) {
				const auto& colorsAccessor {asset.accessors[colorIt->accessorIndex]};
				fastgltf::copyFromAccessor<glm::vec2>(
					asset, colorsAccessor, colors.data() + offset
				);
			} else {
				std::fill(
					colors.begin() + offset,
					colors.begin() + offset + positionAccessor.count,
					glm::vec4 {0, 0, 0, 0}
				);
			}
			const auto texCoordsIt {primitive.findAttribute("TEXCOORD_0")}; // load UVs
			if (texCoordsIt != primitive.attributes.end()) {
				const auto& texCoordsAccessor {asset.accessors[texCoordsIt->accessorIndex]};
				fastgltf::copyFromAccessor<glm::vec2>(
					asset, texCoordsAccessor, texCoords.data() + offset
				);
			} else {
				std::fill(
					texCoords.begin() + offset,
					texCoords.begin() + offset + positionAccessor.count,
					glm::vec2 {std::nan(""), std::nan("")}
				);
			}

			std::vector<uint32_t> indices {}; // load indices
			assert(primitive.indicesAccessor.has_value());
			const auto& indicesAccessor {asset.accessors[primitive.indicesAccessor.value()]};
			indices.resize(indicesAccessor.count);
			fastgltf::copyFromAccessor<uint32_t>(asset, indicesAccessor, indices.data());
			indicesCount += indicesAccessor.count;
			vk::raii::Buffer stagingBuffer {nullptr};
			VmaAllocation stagingAllocation {};
			vk::DeviceSize bufferSize {sizeof(indices[0]) * indices.size()};
			createBuffer(
				bufferSize,
				vk::BufferUsageFlagBits::eTransferSrc,
				VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
				stagingBuffer,
				stagingAllocation
			);
			vmaCopyMemoryToAllocation(
				m_allocator, indices.data(), stagingAllocation, 0, bufferSize
			);
			vk::raii::Buffer indexBuffer {nullptr};
			VmaAllocation indexBufferAllocation {};
			createBuffer(
				bufferSize,
				vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer,
				{},
				indexBuffer,
				indexBufferAllocation
			);
			copyBuffer(stagingBuffer, indexBuffer, bufferSize);
			vmaDestroyBuffer(m_allocator, stagingBuffer.release(), stagingAllocation);

			indexBuffers.push_back(std::move(indexBuffer));
			indexBufferAllocations.push_back(std::move(indexBufferAllocation));

			if (primitive.materialIndex.has_value()) {
				materialIndices.push_back(primitive.materialIndex.has_value());
			} else {
				materialIndices.push_back(-1);
			}

			offset += positionAccessor.count;
		}
		std::vector<Vertex> vertices {};
		vertices.reserve(positions.size());
		for (size_t i {0}; i < positions.size(); ++i) {
			vertices.emplace_back(positions[i], normals[i], colors[i], texCoords[i]);
		}

		vk::raii::Buffer stagingBuffer {nullptr};
		VmaAllocation stagingAllocation {};
		vk::DeviceSize bufferSize {sizeof(vertices[0]) * vertices.size()};
		createBuffer(
			bufferSize,
			vk::BufferUsageFlagBits::eTransferSrc,
			VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
			stagingBuffer,
			stagingAllocation
		);
		vmaCopyMemoryToAllocation(m_allocator, vertices.data(), stagingAllocation, 0, bufferSize);

		vk::raii::Buffer vertexBuffer {nullptr};
		VmaAllocation vertexAllocation {};
		createBuffer(
			bufferSize,
			vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer,
			{},
			vertexBuffer,
			vertexAllocation
		);
		copyBuffer(stagingBuffer, vertexBuffer, bufferSize);
		vmaDestroyBuffer(m_allocator, stagingBuffer.release(), stagingAllocation);

		m_meshes.push_back(
			MeshBuffers {
				std::move(vertexBuffer),
				std::move(vertexAllocation),
				std::move(indexBuffers),
				std::move(indexBufferAllocations),
				indicesCount,
				materialIndices
			}
		);
	}
}

void VulkanInterface::loadMaterials(const fastgltf::Asset& asset) {
	std::vector<MaterialBufferObject> materials {};
	materials.reserve(asset.materials.size());
	for (const auto& material: asset.materials) {
		const auto& pbrData {material.pbrData};

		uint32_t baseColorTextureIndex {static_cast<uint32_t>(-1)};
		glm::vec4 baseColorFactor {
			pbrData.baseColorFactor.x(),
			pbrData.baseColorFactor.y(),
			pbrData.baseColorFactor.z(),
			pbrData.baseColorFactor.w()
		};
		if (pbrData.baseColorTexture.has_value()) {
			assert(pbrData.baseColorTexture->texCoordIndex == 0);
			baseColorTextureIndex = pbrData.baseColorTexture->textureIndex;
		}

		uint32_t metallicRoughnessTextureIndex {static_cast<uint32_t>(-1)};
		glm::vec2 metallicRoughnessFactor {pbrData.metallicFactor, pbrData.roughnessFactor};
		if (pbrData.metallicRoughnessTexture.has_value()) {
			assert(pbrData.metallicRoughnessTexture->texCoordIndex == 0);
			metallicRoughnessTextureIndex = pbrData.metallicRoughnessTexture->textureIndex;
		}

		uint32_t normalTextureIndex {static_cast<uint32_t>(-1)};
		float normalScale {1};
		if (material.normalTexture.has_value()) {
			assert(material.normalTexture->texCoordIndex == 0);
			normalTextureIndex = material.normalTexture->textureIndex;
			normalScale = material.normalTexture->scale;
		}

		uint32_t occlusionTextureIndex {static_cast<uint32_t>(-1)};
		float occlusionStrength {1};
		if (material.occlusionTexture.has_value()) {
			assert(material.occlusionTexture->texCoordIndex == 0);
			occlusionTextureIndex = material.occlusionTexture->textureIndex;
			occlusionStrength = material.occlusionTexture->strength;
		}

		uint32_t emissiveTextureIndex {static_cast<uint32_t>(-1)};
		glm::vec3 emissiveFactor {
			material.emissiveFactor.x(), material.emissiveFactor.y(), material.emissiveFactor.z()
		};
		if (material.emissiveTexture.has_value()) {
			assert(material.emissiveTexture->texCoordIndex == 0);
			emissiveTextureIndex = material.emissiveTexture->textureIndex;
		}

		materials.emplace_back(
			baseColorTextureIndex,
			baseColorFactor,
			metallicRoughnessTextureIndex,
			metallicRoughnessFactor,
			normalTextureIndex,
			normalScale,
			occlusionTextureIndex,
			occlusionStrength,
			emissiveTextureIndex,
			emissiveFactor
		);
	}
	vk::DeviceSize bufferSize {sizeof(materials[0]) * materials.size()};
	vk::raii::Buffer stagingBuffer {nullptr};
	VmaAllocation stagingAllocation {};
	createBuffer(
		bufferSize,
		vk::BufferUsageFlagBits::eTransferSrc,
		VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
		stagingBuffer,
		stagingAllocation
	);
	vmaCopyMemoryToAllocation(m_allocator, materials.data(), stagingAllocation, 0, bufferSize);
	createBuffer(
		bufferSize,
		vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer,
		{},
		m_materialBuffer,
		m_materialAllocation
	);
	copyBuffer(stagingBuffer, m_materialBuffer, bufferSize);
	vmaDestroyBuffer(m_allocator, stagingBuffer.release(), stagingAllocation);
}

void VulkanInterface::createBuffers(const fastgltf::Asset& asset) {
	size_t modelInstanceCount {m_currentScene->getModelInstanceTransforms().size()};
	size_t lightsCount {m_currentScene->getLights().size()};
	for (size_t frameInFlight {0}; frameInFlight < MAX_FRAMES_IN_FLIGHT; ++frameInFlight) {
		vk::DeviceSize bufferSize {sizeof(ModelTransformBufferObject) * modelInstanceCount};
		createBuffer(
			bufferSize,
			vk::BufferUsageFlagBits::eStorageBuffer,
			VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
				VMA_ALLOCATION_CREATE_MAPPED_BIT,
			m_modelTransformBuffers[frameInFlight],
			m_modelTransformAllocations[frameInFlight]
		);

		bufferSize = sizeof(VPTransformBufferObject);
		createBuffer(
			bufferSize,
			vk::BufferUsageFlagBits::eUniformBuffer,
			VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
				VMA_ALLOCATION_CREATE_MAPPED_BIT,
			m_vpTransformBuffers[frameInFlight],
			m_vpTransformAllocations[frameInFlight]
		);

		bufferSize = sizeof(LightBufferObject) * lightsCount;
		createBuffer(
			bufferSize,
			vk::BufferUsageFlagBits::eUniformBuffer,
			VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
				VMA_ALLOCATION_CREATE_MAPPED_BIT,
			m_lightBuffers[frameInFlight],
			m_lightAllocations[frameInFlight]
		);
	}
}

void VulkanInterface::createDescriptorSets(const fastgltf::Asset& asset) {
	size_t textureCount {asset.textures.size()};
	createDescriptorPool(textureCount);
	m_descriptorSetLayout = createDescriptorSetLayout(textureCount);
	std::vector<vk::DescriptorSetLayout> layouts {MAX_FRAMES_IN_FLIGHT, m_descriptorSetLayout};
	vk::DescriptorSetAllocateInfo allocInfo {
		.descriptorPool = m_descriptorPool,
		.descriptorSetCount = static_cast<uint32_t>(layouts.size()),
		.pSetLayouts = layouts.data()
	};
	m_descriptorSets = m_device.allocateDescriptorSets(allocInfo);

	for (size_t frameInFlight {0}; frameInFlight < MAX_FRAMES_IN_FLIGHT; ++frameInFlight) {
		vk::DescriptorBufferInfo modelTransformBufferInfo {
			.buffer = m_modelTransformBuffers[frameInFlight],
			.offset = 0,
			.range = sizeof(ModelTransformBufferObject) *
					 m_currentScene->getModelInstanceTransforms().size()
		};

		vk::DescriptorBufferInfo vpTransformBufferInfo {
			.buffer = m_vpTransformBuffers[frameInFlight],
			.offset = 0,
			.range = sizeof(VPTransformBufferObject)
		};

		vk::DescriptorBufferInfo materialBufferInfo {
			.buffer = m_materialBuffer,
			.offset = 0,
			.range = sizeof(MaterialBufferObject) * asset.materials.size()
		};

		std::vector<vk::DescriptorImageInfo> imageInfos {};
		imageInfos.reserve(textureCount);
		for (const auto& texture: asset.textures) {
			imageInfos.push_back(
				{.sampler = m_textureSamplers[texture.samplerIndex.value()],
				 .imageView = m_textureImageViews[texture.basisuImageIndex.value()],
				 .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal}
			);
		}

		vk::DescriptorBufferInfo lightBufferInfo {
			m_lightBuffers[frameInFlight],
			0,
			sizeof(LightBufferObject) * m_currentScene->getLights().size()
		};
		std::array<vk::WriteDescriptorSet, 5> descriptorWrites {
			{{.dstSet = m_descriptorSets[frameInFlight],
			  .dstBinding = 0,
			  .dstArrayElement = 0,
			  .descriptorCount = 1,
			  .descriptorType = vk::DescriptorType::eStorageBuffer,
			  .pBufferInfo = &modelTransformBufferInfo},
			 {.dstSet = m_descriptorSets[frameInFlight],
			  .dstBinding = 1,
			  .dstArrayElement = 0,
			  .descriptorCount = 1,
			  .descriptorType = vk::DescriptorType::eUniformBuffer,
			  .pBufferInfo = &vpTransformBufferInfo},
			 {.dstSet = m_descriptorSets[frameInFlight],
			  .dstBinding = 2,
			  .dstArrayElement = 0,
			  .descriptorCount = 1,
			  .descriptorType = vk::DescriptorType::eStorageBuffer,
			  .pBufferInfo = &materialBufferInfo},
			 {
				 .dstSet = m_descriptorSets[frameInFlight],
				 .dstBinding = 3,
				 .dstArrayElement = 0,
				 .descriptorCount = static_cast<uint32_t>(textureCount),
				 .descriptorType = vk::DescriptorType::eCombinedImageSampler,
				 .pImageInfo = imageInfos.data(),
			 },
			 {.dstSet = m_descriptorSets[frameInFlight],
			  .dstBinding = 4,
			  .dstArrayElement = 0,
			  .descriptorCount = 1,
			  .descriptorType = vk::DescriptorType::eUniformBuffer,
			  .pBufferInfo = &lightBufferInfo}}
		};
		m_device.updateDescriptorSets(descriptorWrites, {});
	}
}

void VulkanInterface::createGraphicsPipeline() {
	vk::raii::ShaderModule shaderModule {createShaderModule(readFile(SHADER_PATH "/slang.spv"))};

	vk::PipelineShaderStageCreateInfo vertShaderStageInfo {
		.stage = vk::ShaderStageFlagBits::eVertex, .module = shaderModule, .pName = "vertMain"
	};
	vk::PipelineShaderStageCreateInfo fragShaderStageInfo {
		.stage = vk::ShaderStageFlagBits::eFragment, .module = shaderModule, .pName = "fragMain"
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
		.topology = vk::PrimitiveTopology::eTriangleList, .primitiveRestartEnable = vk::False
	};

	vk::PipelineViewportStateCreateInfo viewportState {.viewportCount = 1, .scissorCount = 1};

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
		.rasterizationSamples = vk::SampleCountFlagBits::e1, .sampleShadingEnable = vk::False
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
		.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
						  vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA
	};

	vk::PipelineColorBlendStateCreateInfo colorBlending {
		.logicOpEnable = vk::False,
		.logicOp = vk::LogicOp::eCopy,
		.attachmentCount = 1,
		.pAttachments = &colorBlendAttachment
	};

	std::vector dynamicStates {vk::DynamicState::eViewport, vk::DynamicState::eScissor};

	vk::PipelineDynamicStateCreateInfo dynamicState {
		.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
		.pDynamicStates = dynamicStates.data()
	};

	vk::PushConstantRange pushConstantRange {
		.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
		.offset = 0,
		.size = sizeof(PushConstants)
	};

	vk::PipelineLayoutCreateInfo pipelineLayoutInfo {
		.setLayoutCount = 1,
		.pSetLayouts = &*m_descriptorSetLayout,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &pushConstantRange
	};

	m_pipelineLayout = {m_device, pipelineLayoutInfo};

	vk::Format depthFormat {findDepthFormat()};

	vk::StructureChain<vk::GraphicsPipelineCreateInfo, vk::PipelineRenderingCreateInfo>
		pipelineCreateInfoChain {
			{.stageCount = 2,
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
			 .renderPass = nullptr},
			{.colorAttachmentCount = 1,
			 .pColorAttachmentFormats = &m_swapChainSurfaceFormat.format,
			 .depthAttachmentFormat = depthFormat}
		};
	m_graphicsPipeline = {
		m_device, nullptr, pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>()
	};
}
