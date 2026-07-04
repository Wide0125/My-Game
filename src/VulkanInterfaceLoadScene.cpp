#include <print>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/hash.hpp>

#include <ktx.h>
#include <ktxvulkan.h>

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
		vkTexture.image = nullptr;
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
		// TODO continue with gltf loading
	}
}
