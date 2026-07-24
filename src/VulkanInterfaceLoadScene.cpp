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

void VulkanInterface::loadScene(const fastgltf::Asset& asset, Scene* pScene) {
	m_currentScene = pScene;
	createTextureImages(asset);
	createTextureSamplers(asset);
	loadMeshes(asset);
	createAccelerationStructures();
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
	m_meshes.reserve(asset.meshes.size());

	std::vector<glm::vec3> positions {};
	std::vector<glm::vec3> normals {};
	std::vector<glm::vec4> colors {};
	std::vector<glm::vec2> texCoords {};
	std::vector<glm::vec4> tangents {};

	std::vector<uint32_t> indices {};

	uint32_t verticesRunningCount {0};
	uint32_t indicesRunningCount {0};

	for (const auto& mesh: asset.meshes) {
		std::vector<Mesh::SubMesh> subMeshes {};
		subMeshes.reserve(mesh.primitives.size());
		m_subMeshCount += mesh.primitives.size();

		for (const auto& primitive: mesh.primitives) {

			Mesh::SubMesh subMesh {.indexStart = indicesRunningCount};

			const auto& positionAccessor { // load vertices
				asset.accessors[primitive.findAttribute("POSITION")->accessorIndex]
			};

			size_t vertexCount {positionAccessor.count};
			positions.resize(positions.size() + vertexCount);
			normals.resize(normals.size() + vertexCount);
			colors.resize(colors.size() + vertexCount);
			texCoords.resize(texCoords.size() + vertexCount);
			tangents.resize(tangents.size() + vertexCount);

			fastgltf::copyFromAccessor<glm::vec3>(
				asset, positionAccessor, positions.data() + verticesRunningCount
			);
			const auto normalIt {primitive.findAttribute("NORMAL")}; // load normals
			const auto& normalsAccessor {asset.accessors[normalIt->accessorIndex]};
			fastgltf::copyFromAccessor<glm::vec3>(
				asset, normalsAccessor, normals.data() + verticesRunningCount
			);
			const auto colorIt {primitive.findAttribute("COLOR_0")}; // load colors
			if (colorIt != primitive.attributes.end()) {
				const auto& colorsAccessor {asset.accessors[colorIt->accessorIndex]};
				fastgltf::copyFromAccessor<glm::vec4>(
					asset, colorsAccessor, colors.data() + verticesRunningCount
				);
			}
			const auto texCoordsIt {primitive.findAttribute("TEXCOORD_0")}; // load UVs
			const auto& texCoordsAccessor {asset.accessors[texCoordsIt->accessorIndex]};
			fastgltf::copyFromAccessor<glm::vec2>(
				asset, texCoordsAccessor, texCoords.data() + verticesRunningCount
			);

			const auto tangentIt {primitive.findAttribute("TANGENT")};
			const auto& tangentAccessor {asset.accessors[tangentIt->accessorIndex]};
			fastgltf::copyFromAccessor<glm::vec4>(
				asset, tangentAccessor, tangents.data() + verticesRunningCount
			);

			assert(primitive.indicesAccessor.has_value());
			const auto& indicesAccessor {asset.accessors[primitive.indicesAccessor.value()]};
			indices.resize(indices.size() + indicesAccessor.count);
			fastgltf::copyFromAccessor<uint32_t>(
				asset, indicesAccessor, indices.data() + indicesRunningCount
			);
			subMesh.indexCount = indicesAccessor.count;
			subMesh.vertexOffset = verticesRunningCount;
			subMesh.maxIndex = *std::ranges::max_element(
				indices.begin() + indicesRunningCount,
				indices.begin() + indicesRunningCount + indicesAccessor.count
			);

			if (primitive.materialIndex.has_value()) {
				subMesh.materialIndex = primitive.materialIndex.value();
			} else {
				subMesh.materialIndex = (-1);
			}

			verticesRunningCount += vertexCount;
			indicesRunningCount += indicesAccessor.count;

			switch (asset.materials[primitive.materialIndex.value()].alphaMode) {
				case fastgltf::AlphaMode::Opaque:
				case fastgltf::AlphaMode::Mask:
					subMesh.opaque = true;
					break;
				case fastgltf::AlphaMode::Blend:
					subMesh.opaque = false;
					break;
			}

			subMeshes.push_back(std::move(subMesh));
		}
		m_meshes.emplace_back(std::move(subMeshes));
	}
	std::vector<Vertex> vertices {};
	vertices.reserve(positions.size());
	for (size_t i {0}; i < positions.size(); ++i) {
		vertices.emplace_back(positions[i], normals[i], colors[i], texCoords[i], tangents[i]);
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

	createBuffer(
		bufferSize,
		vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer |
			vk::BufferUsageFlagBits::eShaderDeviceAddress |
			vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR,
		{},
		m_vertexBuffer,
		m_vertexAllocation
	);
	copyBuffer(stagingBuffer, m_vertexBuffer, bufferSize);
	vmaDestroyBuffer(m_allocator, stagingBuffer.release(), stagingAllocation);

	bufferSize = sizeof(indices[0]) * indices.size();
	createBuffer(
		bufferSize,
		vk::BufferUsageFlagBits::eTransferSrc,
		VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
		stagingBuffer,
		stagingAllocation
	);
	vmaCopyMemoryToAllocation(m_allocator, indices.data(), stagingAllocation, 0, bufferSize);

	createBuffer(
		bufferSize,
		vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer |
			vk::BufferUsageFlagBits::eShaderDeviceAddress |
			vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR,
		{},
		m_indexBuffer,
		m_indexAllocation
	);
	copyBuffer(stagingBuffer, m_indexBuffer, bufferSize);

	vmaDestroyBuffer(m_allocator, stagingBuffer.release(), stagingAllocation);
}

void VulkanInterface::createAccelerationStructures() {
	vk::DeviceAddress vertexAddress {m_device.getBufferAddress({.buffer = m_vertexBuffer})};
	vk::DeviceAddress indexAddress {m_device.getBufferAddress({.buffer = m_indexBuffer})};
	for (size_t blasIndex {0}; blasIndex < m_blasBuffers.size(); ++blasIndex) {
		vmaDestroyBuffer(
			m_allocator, m_blasBuffers[blasIndex].release(), m_blasAllocations[blasIndex]
		);
	}
	m_blasBuffers.clear();
	m_blasAllocations.clear();
	m_blasHandles.clear();

	m_blasBuffers.reserve(m_subMeshCount);
	m_blasAllocations.reserve(m_subMeshCount);
	m_blasHandles.reserve(m_subMeshCount);

	for (const auto& mesh: m_meshes) {
		for (const auto& subMesh: mesh.subMeshes) {
			vk::AccelerationStructureGeometryTrianglesDataKHR trianglesData {
				.vertexFormat = vk::Format::eR32G32B32Sfloat,
				.vertexData = vertexAddress,
				.vertexStride = sizeof(Vertex),
				.maxVertex = subMesh.maxIndex,
				.indexType = vk::IndexType::eUint32,
				.indexData = indexAddress,
			};
			vk::AccelerationStructureGeometryDataKHR geometryData {trianglesData};
			vk::AccelerationStructureGeometryKHR blasGeometry {
				.geometryType = vk::GeometryTypeKHR::eTriangles,
				.geometry = geometryData,
				.flags = subMesh.opaque ? vk::GeometryFlagBitsKHR::eOpaque : vk::GeometryFlagsKHR(0)
			};
			vk::AccelerationStructureBuildGeometryInfoKHR blasBuildGeometryInfo {
				.type = vk::AccelerationStructureTypeKHR::eBottomLevel,
				.mode = vk::BuildAccelerationStructureModeKHR::eBuild,
				.geometryCount = 1,
				.pGeometries = &blasGeometry
			};

			vk::AccelerationStructureBuildSizesInfoKHR blasBuildSizes {
				m_device.getAccelerationStructureBuildSizesKHR(
					vk::AccelerationStructureBuildTypeKHR::eDevice,
					blasBuildGeometryInfo,
					{subMesh.indexCount / 3}
				)
			};

			vk::raii::Buffer scratchBuffer {nullptr};
			VmaAllocation scratchAllocation {};
			createBuffer(
				blasBuildSizes.buildScratchSize,
				vk::BufferUsageFlagBits::eStorageBuffer |
					vk::BufferUsageFlagBits::eShaderDeviceAddress,
				{},
				scratchBuffer,
				scratchAllocation,
				m_accelerationStructureScratchOffset
			);
			vk::BufferDeviceAddressInfo scratchAddressInfo {.buffer = scratchBuffer};
			vk::DeviceAddress scratchAddress {m_device.getBufferAddress(scratchAddressInfo)};
			blasBuildGeometryInfo.scratchData.deviceAddress = scratchAddress;

			vk::raii::Buffer blasBuffer {nullptr};
			VmaAllocation blasAllocation {};
			createBuffer(
				blasBuildSizes.accelerationStructureSize,
				vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR |
					vk::BufferUsageFlagBits::eShaderDeviceAddress |
					vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR,
				{},
				blasBuffer,
				blasAllocation
			);
			m_blasBuffers.emplace_back(std::move(blasBuffer));
			m_blasAllocations.emplace_back(blasAllocation);

			vk::AccelerationStructureCreateInfoKHR blasCreateInfo {
				.buffer = m_blasBuffers[m_blasBuffers.size() - 1],
				.offset = 0,
				.size = blasBuildSizes.accelerationStructureSize,
				.type = vk::AccelerationStructureTypeKHR::eBottomLevel,
			};
			m_blasHandles.emplace_back(m_device.createAccelerationStructureKHR(blasCreateInfo));

			blasBuildGeometryInfo.dstAccelerationStructure =
				m_blasHandles[m_blasHandles.size() - 1];

			vk::AccelerationStructureBuildRangeInfoKHR blasRangeInfo {
				.primitiveCount = subMesh.indexCount / 3,
				.primitiveOffset = static_cast<uint32_t>(subMesh.indexStart * sizeof(uint32_t)),
				.firstVertex = subMesh.vertexOffset,
				.transformOffset = 0
			};
			auto commandBuffer {beginSingleTimeCommands()};
			commandBuffer->buildAccelerationStructuresKHR(
				{blasBuildGeometryInfo}, {&blasRangeInfo}
			);
			endSingleTimeCommands(*commandBuffer);

			vmaDestroyBuffer(m_allocator, scratchBuffer.release(), scratchAllocation);
		}
	}
	int modelInstanceRunningCount {0};
	for (
		int meshIndex {0}; meshIndex < m_currentScene->getModelInstancesPerMesh().size();
		++meshIndex
	) {
		for (
			int modelInstanceIndex {0};
			modelInstanceIndex < m_currentScene->getModelInstancesPerMesh()[meshIndex];
			++modelInstanceIndex
		) {
			vk::AccelerationStructureDeviceAddressInfoKHR addressInfo {
				.accelerationStructure = *m_blasHandles[meshIndex]
			};
			vk::DeviceAddress blasDeviceAddress =
				m_device.getAccelerationStructureAddressKHR(addressInfo);

			glm::mat4 currTransform {m_currentScene
										 ->getModelInstanceTransforms()[modelInstanceRunningCount]
										 .modelTransform};
			std::array<std::array<float, 4>, 3> transformArray {
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

			vk::AccelerationStructureInstanceKHR instance {
				.transform = {transformArray},
				.instanceCustomIndex = static_cast<uint32_t>(modelInstanceRunningCount),
				.mask = 0xFF,
				.accelerationStructureReference = blasDeviceAddress
			};

			m_blasInstances.push_back(instance);
			++modelInstanceRunningCount;
		}
	}
	vk::DeviceSize instanceBufferSize {sizeof(m_blasInstances[0]) * m_blasInstances.size()};
	createBuffer(
		instanceBufferSize,
		vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eTransferDst |
			vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR,
		VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
		m_blasInstanceBuffer,
		m_blasInstanceAllocation
	);
	vmaCopyMemoryToAllocation(
		m_allocator, m_blasInstances.data(), m_blasInstanceAllocation, 0, instanceBufferSize
	);

	vk::BufferDeviceAddressInfo instanceAddressInfo {.buffer = m_blasInstanceBuffer};
	vk::DeviceAddress instanceAddress {m_device.getBufferAddress(instanceAddressInfo)};

	vk::AccelerationStructureGeometryInstancesDataKHR instancesData {
		.arrayOfPointers = vk::False, .data = instanceAddress
	};
	vk::AccelerationStructureGeometryDataKHR geometryData {instancesData};

	vk::AccelerationStructureGeometryKHR tlasGeometry {
		.geometryType = vk::GeometryTypeKHR::eInstances, .geometry = geometryData
	};
	vk::AccelerationStructureBuildGeometryInfoKHR tlasBuildGeometryInfo {
		.type = vk::AccelerationStructureTypeKHR::eTopLevel,
		.flags = vk::BuildAccelerationStructureFlagBitsKHR::eAllowUpdate,
		.mode = vk::BuildAccelerationStructureModeKHR::eBuild,
		.geometryCount = 1,
		.pGeometries = &tlasGeometry
	};
	vk::AccelerationStructureBuildSizesInfoKHR tlasBuildSizes {
		m_device.getAccelerationStructureBuildSizesKHR(
			vk::AccelerationStructureBuildTypeKHR::eDevice,
			tlasBuildGeometryInfo,
			{static_cast<uint32_t>(m_blasInstances.size())}
		)
	};

	createBuffer(
		tlasBuildSizes.buildScratchSize,
		vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress,
		{},
		m_tlasScratchBuffer,
		m_tlasScratchAllocation,
		m_accelerationStructureScratchOffset
	);

	vk::BufferDeviceAddressInfo scratchAddressInfo {.buffer = *m_tlasScratchBuffer};
	vk::DeviceAddress scratchAddress {m_device.getBufferAddress(scratchAddressInfo)};
	tlasBuildGeometryInfo.scratchData.deviceAddress = scratchAddress;

	createBuffer(
		tlasBuildSizes.accelerationStructureSize,
		vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR |
			vk::BufferUsageFlagBits::eShaderDeviceAddress |
			vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR,
		{},
		m_tlasBuffer,
		m_tlasAllocation
	);

	vk::AccelerationStructureCreateInfoKHR tlasCreateInfo {
		.buffer = m_tlasBuffer,
		.offset = 0,
		.size = tlasBuildSizes.accelerationStructureSize,
		.type = vk::AccelerationStructureTypeKHR::eTopLevel
	};

	m_tlas = m_device.createAccelerationStructureKHR(tlasCreateInfo);

	tlasBuildGeometryInfo.dstAccelerationStructure = m_tlas;

	vk::AccelerationStructureBuildRangeInfoKHR tlasRangeInfo {
		.primitiveCount = static_cast<uint32_t>(m_blasInstances.size()),
		.primitiveOffset = 0,
		.firstVertex = 0,
		.transformOffset = 0
	};

	auto commandBuffer {beginSingleTimeCommands()};
	commandBuffer->buildAccelerationStructuresKHR({tlasBuildGeometryInfo}, {&tlasRangeInfo});
	endSingleTimeCommands(*commandBuffer);
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
		MaterialBufferObject::AlphaMode alphaMode {
			static_cast<MaterialBufferObject::AlphaMode>(material.alphaMode)
		};
		float alphaCutoff {material.alphaCutoff};

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
			emissiveFactor,
			alphaMode,
			alphaCutoff
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
			vk::BufferUsageFlagBits::eStorageBuffer,
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

		vk::WriteDescriptorSetAccelerationStructureKHR accelerationStructureInfo {
			.accelerationStructureCount = 1, .pAccelerationStructures = &*m_tlas
		};
		std::array<vk::WriteDescriptorSet, 6> descriptorWrites {
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
			  .descriptorType = vk::DescriptorType::eStorageBuffer,
			  .pBufferInfo = &lightBufferInfo},
			 {.pNext = &accelerationStructureInfo,
			  .dstSet = m_descriptorSets[frameInFlight],
			  .dstBinding = 5,
			  .dstArrayElement = 0,
			  .descriptorCount = 1,
			  .descriptorType = vk::DescriptorType::eAccelerationStructureKHR}}
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
		.blendEnable = vk::True,
		.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha,
		.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
		.colorBlendOp = vk::BlendOp::eAdd,
		.srcAlphaBlendFactor = vk::BlendFactor::eOne,
		.dstAlphaBlendFactor = vk::BlendFactor::eZero,
		.alphaBlendOp = vk::BlendOp::eAdd,
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
