#include <print>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/hash.hpp>

#include <ktx.h>

#include <fastgltf/types.hpp>
#include <fastgltf/util.hpp>

#include "VulkanInterface.hpp"

void VulkanInterface::loadScene(const fastgltf::Asset& asset) {
	createTextureImages(asset);
	createTextureSamplers(asset);
	loadModels(asset);
}

void VulkanInterface::createTextureImages(const fastgltf::Asset& asset) {
	m_textureImages.clear();
	m_textureImageAllocations.clear();
	m_textureImageViews.clear();

	m_textureImages.reserve(asset.images.size());
	m_textureImageAllocations.reserve(asset.images.size());
	m_textureImageViews.reserve(asset.images.size());
	for (const auto& image: asset.images) {
		int texChannels {};

		ktxTexture* kTexture {};
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
					const std::string path {
						std::string {SCENE_PATH} + "/" + std::string {filePath.uri.path()}
					};
					result = ktxTexture_CreateFromNamedFile(
						path.c_str(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &kTexture
					);
				},
				[&](const fastgltf::sources::Vector& vector) {
					std::println(
						"Loading image directly from "
						"memory..."
					);
					result = ktxTexture_CreateFromMemory(
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
					auto& bufferView = asset.bufferViews[bufferViewSource.bufferViewIndex];
					auto& buffer = asset.buffers[bufferView.bufferIndex];
					std::visit(
						fastgltf::visitor {
							[](auto& arg) {},
							[&](const fastgltf::sources::Array& array) {
								result = ktxTexture_CreateFromMemory(
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
		); // load image
		if (result != KTX_SUCCESS) {
			throw std::runtime_error("Failed to load texture image!");
		}
		uint32_t texWidth {kTexture->baseWidth};
		uint32_t texHeight {kTexture->baseHeight};
		ktx_size_t imageSize {ktxTexture_GetDataSize(kTexture)};
		mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(texWidth, texHeight)))) + 1;

		vk::raii::Buffer stagingBuffer {nullptr};
		VmaAllocation stagingAllocation {};
		createBuffer(
			imageSize,
			vk::BufferUsageFlagBits::eTransferSrc,
			VmaAllocationCreateFlagBits::VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
			stagingBuffer,
			stagingAllocation
		);

		void* stagingData {};
		if (vmaMapMemory(m_allocator, stagingAllocation, &stagingData) != VK_SUCCESS) {
			throw std::runtime_error("Failed to map memory for textures!");
		}
		memcpy(stagingData, ktxTexture_GetData(kTexture), imageSize);
		vmaUnmapMemory(m_allocator, stagingAllocation);

		ktxTexture_Destroy(kTexture);

		vk::raii::Image imageTemp {nullptr};
		VmaAllocation imageAllocationTemp {};
		createImage(
			texWidth,
			texHeight,
			mipLevels,
			vk::Format::eR8G8B8A8Srgb,
			vk::ImageTiling::eOptimal,
			vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst |
				vk::ImageUsageFlagBits::eSampled,
			imageTemp,
			imageAllocationTemp
		);

		transitionImageLayout(
			imageTemp, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, mipLevels
		);
		copyBufferToImage(stagingBuffer, imageTemp, texWidth, texHeight);
		// vmaDestroyBuffer(m_allocator, *stagingBuffer, stagingAllocation);

		generateMipmaps(imageTemp, vk::Format::eR8G8B8A8Srgb, texWidth, texHeight, mipLevels);

		auto imageViewTemp {createImageView(
			imageTemp, vk::Format::eR8G8B8A8Srgb, vk::ImageAspectFlagBits::eColor, mipLevels
		)};

		m_textureImages.push_back(std::move(imageTemp));
		m_textureImageAllocations.push_back(std::move(imageAllocationTemp));
		m_textureImageViews.push_back(
			std::move(imageViewTemp)
		); // TODO rewrite texture loader entirely to support ktx compressed textures
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

void VulkanInterface::loadModels(const fastgltf::Asset& asset) {
	for (const auto& mesh: asset.meshes) {
	}
}
