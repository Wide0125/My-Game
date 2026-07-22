#ifndef VULKANINTERFACE_HPP
#define VULKANINTERFACE_HPP

#include <cstdint>
#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

#include "vk_mem_alloc.h"

#include <GLFW/glfw3.h>

#include <glm/glm.hpp>

#include "fastgltf/types.hpp"

class Scene;

struct ModelTransformBufferObject {
	glm::mat4 modelTransform {};
	glm::mat3 normalMatrix {};
};

struct VPTransformBufferObject {
	glm::vec3 cameraPosition {};
	glm::mat4 viewTransform {};
	glm::mat4 projectionTransform {};
	int lightCount {};
};

struct PushConstants {
	uint32_t materialIndex {};
};

struct Vertex {
	glm::vec3 pos;
	glm::vec3 normal;
	glm::vec4 color;
	glm::vec2 texCoord;
	glm::vec4 tangent;

	// Binding and attribute descriptions for Vulkan
	static vk::VertexInputBindingDescription getBindingDescription() {
		return {0, sizeof(Vertex), vk::VertexInputRate::eVertex};
	}

	static std::array<vk::VertexInputAttributeDescription, 5> getAttributeDescriptions() {
		return {
			vk::VertexInputAttributeDescription(
				0, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, pos)
			),
			vk::VertexInputAttributeDescription(
				1, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, normal)
			),
			vk::VertexInputAttributeDescription(
				2, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, color)
			),
			vk::VertexInputAttributeDescription(
				3, 0, vk::Format::eR32G32Sfloat, offsetof(Vertex, texCoord)
			),
			vk::VertexInputAttributeDescription(
				4, 0, vk::Format::eR32G32B32A32Sfloat, offsetof(Vertex, tangent)
			)
		};
	}
};

struct LightBufferObject {
	enum LightType { directional, spot, point };
	LightType type {};
	glm::vec3 color {};
	float intensity {};
	float range {};

	glm::vec3 position {};
	glm::vec3 direction {};
};

struct MaterialBufferObject {
	uint32_t baseColorTextureIndex {};
	glm::vec4 baseColorFactor {};

	uint32_t metallicRoughnessTextureIndx {};
	glm::vec2 metallicRoughnessFactor {};

	uint32_t normalTextureIndex {};
	float normalScale {};

	uint32_t occlusionTextureIndex {};
	float occlusionStrength {};

	uint32_t emissiveTextureIndex {};
	glm::vec3 emissiveFactor {};
};

class VulkanInterface {
  public:
	VulkanInterface();
	~VulkanInterface() { cleanup(); }
	void loadScene(const fastgltf::Asset& asset, Scene* scene); // load gltf information onto GPU

	void waitIdle() { m_device.waitIdle(); }

	void drawFrame();

	GLFWwindow* const getWindow() const { return m_window; }

  private:
	static constexpr int MAX_FRAMES_IN_FLIGHT {2};

	static constexpr int WIDTH {800};
	static constexpr int HEIGHT {600};

	bool rayTracingAvailable {false};

	GLFWwindow* m_window {nullptr}; // GLFW window object

	vk::raii::Context m_context {};
	vk::raii::Instance m_instance {nullptr}; // Vulkan instance

	Scene* m_currentScene {nullptr};

	vk::raii::SurfaceKHR m_surface {nullptr}; // surface for Vulkan to draw onto

	std::vector<vk::raii::PhysicalDevice> m_availablePhysicalDevices {};
	vk::raii::PhysicalDevice m_physicalDevice {
		nullptr
	}; // object representation of selected physical GPU
	vk::raii::Device m_device {nullptr}; // interface to interact with GPU
	uint32_t m_queueFamilyIndex {~0u};	 // index of selected queue family
	vk::raii::Queue m_queue {nullptr};	 // selected queue
	std::vector<const char*> m_requiredDeviceExtensions {
		vk::KHRSwapchainExtensionName,
		vk::KHRAccelerationStructureExtensionName,
		vk::KHRRayQueryExtensionName,
		vk::KHRAccelerationStructureExtensionName,
		vk::KHRDeferredHostOperationsExtensionName,
		vk::KHRBufferDeviceAddressExtensionName
	};

	VmaAllocator m_allocator {};

	vk::raii::SwapchainKHR m_swapChain {nullptr};
	std::vector<vk::Image> m_swapChainImages {};
	vk::Extent2D m_swapChainExtent {};
	vk::SurfaceFormatKHR m_swapChainSurfaceFormat {};
	std::vector<vk::raii::ImageView> m_swapChainImageViews {};

	vk::raii::CommandPool m_commandPool {nullptr};
	std::vector<vk::raii::CommandBuffer> m_commandBuffers {};

	vk::raii::Image m_depthImage {nullptr};
	VmaAllocation m_depthImageAllocation {nullptr};
	vk::raii::ImageView m_depthImageView {nullptr};

	vk::raii::Buffer m_vertexBuffer {nullptr};
	VmaAllocation m_vertexAllocation {};
	vk::raii::Buffer m_indexBuffer {nullptr};
	VmaAllocation m_indexAllocation {};
	struct Mesh {
		struct SubMesh {
			uint32_t indexStart {};
			uint32_t indexCount {};
			uint32_t maxIndex {};
			uint32_t vertexOffset {};
			uint32_t materialIndex {};
		};
		std::vector<SubMesh> subMeshes {};
	};
	std::vector<Mesh> m_meshes {};
	int m_subMeshCount {0};

	std::vector<vk::raii::Semaphore> m_presentCompleteSemaphores {};
	std::vector<vk::raii::Semaphore> m_renderFinishedSemaphores {};
	std::vector<vk::raii::Fence> m_inFlightFences {};

	std::vector<vk::raii::Image> m_textureImages {};
	std::vector<vk::raii::DeviceMemory> m_textureImageMemories {};
	std::vector<vk::raii::ImageView> m_textureImageViews {};
	std::vector<vk::raii::Sampler> m_textureSamplers {};

	vk::raii::Buffer m_materialBuffer {nullptr};
	VmaAllocation m_materialAllocation {};

	std::array<vk::raii::Buffer, MAX_FRAMES_IN_FLIGHT> m_modelTransformBuffers {
		{{nullptr}, {nullptr}}
	};
	std::array<VmaAllocation, MAX_FRAMES_IN_FLIGHT> m_modelTransformAllocations {};

	std::array<vk::raii::Buffer, MAX_FRAMES_IN_FLIGHT> m_vpTransformBuffers {
		{{nullptr}, {nullptr}}
	};
	std::array<VmaAllocation, MAX_FRAMES_IN_FLIGHT> m_vpTransformAllocations {};

	std::array<vk::raii::Buffer, MAX_FRAMES_IN_FLIGHT> m_lightBuffers {{{nullptr}, {nullptr}}};
	std::array<VmaAllocation, MAX_FRAMES_IN_FLIGHT> m_lightAllocations {};

	vk::raii::DescriptorSetLayout m_descriptorSetLayout {nullptr};
	vk::raii::DescriptorPool m_descriptorPool {nullptr};
	std::vector<vk::raii::DescriptorSet> m_descriptorSets {};

	vk::raii::Pipeline m_graphicsPipeline {nullptr};
	vk::raii::PipelineLayout m_pipelineLayout {nullptr};

	std::vector<vk::raii::Buffer> m_blasBuffers {};
	std::vector<VmaAllocation> m_blasAllocations {};
	std::vector<vk::raii::AccelerationStructureKHR> m_blasHandles {};

	std::vector<vk::AccelerationStructureInstanceKHR> m_blasInstances {};
	vk::raii::Buffer m_blasInstanceBuffer {nullptr};
	VmaAllocation m_blasInstanceAllocation {};

	vk::raii::Buffer m_tlasBuffer {nullptr};
	VmaAllocation m_tlasAllocation {nullptr};
	vk::raii::Buffer m_tlasScratchBuffer {nullptr};
	VmaAllocation m_tlasScratchAllocation {nullptr};
	vk::raii::AccelerationStructureKHR m_tlas {nullptr};

	vk::DeviceSize m_accelerationStructureScratchOffset {};

	uint32_t m_frameIndex {0};

	void initWindow(); // initialize GLFW window for Vulkan
	void initVulkan(); // initialize Vulkan
	void cleanup() {
		m_textureImages.clear();
		vmaDestroyImage(m_allocator, m_depthImage.release(), m_depthImageAllocation);
		vmaDestroyBuffer(m_allocator, m_materialBuffer.release(), m_materialAllocation);
		vmaDestroyBuffer(m_allocator, m_vertexBuffer.release(), m_vertexAllocation);
		vmaDestroyBuffer(m_allocator, m_indexBuffer.release(), m_indexAllocation);
		for (int i {0}; i < MAX_FRAMES_IN_FLIGHT; ++i) {
			vmaDestroyBuffer(
				m_allocator, m_modelTransformBuffers[i].release(), m_modelTransformAllocations[i]
			);
			vmaDestroyBuffer(
				m_allocator, m_vpTransformBuffers[i].release(), m_vpTransformAllocations[i]
			);
			vmaDestroyBuffer(m_allocator, m_lightBuffers[i].release(), m_lightAllocations[i]);
		}
		for (int blasIndex {0}; blasIndex < m_blasBuffers.size(); ++blasIndex) {
			vmaDestroyBuffer(
				m_allocator, m_blasBuffers[blasIndex].release(), m_blasAllocations[blasIndex]
			);
		}
		vmaDestroyBuffer(m_allocator, m_blasInstanceBuffer.release(), m_blasInstanceAllocation);
		vmaDestroyBuffer(m_allocator, m_tlasBuffer.release(), m_tlasAllocation);
		vmaDestroyBuffer(m_allocator, m_tlasScratchBuffer.release(), m_tlasScratchAllocation);
		vmaDestroyAllocator(m_allocator);
		glfwDestroyWindow(m_window);
		glfwTerminate();
	}

	void createInstance(); // initialize Vulkan
	void createSurface();  // create surface for Vulkan to draw on, linked to
						   // the created GLFW window

	bool isDeviceSuitable(const vk::raii::PhysicalDevice&) const;
	void pickPhysicalDevice();	// pick physical GPU to run Vulkan on
	void createLogicalDevice(); // create logical device to interface with
								// physical GPU

	void createVmaAllocator(); // initialize VMA library

	vk::Extent2D
	chooseSwapExtent(const vk::SurfaceCapabilitiesKHR&) const; // choose extent(size) of swap chain
	static uint32_t chooseSwapMinImageCount(const vk::SurfaceCapabilitiesKHR&);
	static vk::SurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>&);
	void createSwapChain();			  // create swap chain of surfaces
	void createSwapChainImageViews(); // create image views of swap chain images

	void createCommandPool();

	uint32_t findMemoryType(uint32_t, vk::MemoryPropertyFlags) const;
	void createImage(
		uint32_t,
		uint32_t,
		uint32_t,
		vk::Format,
		vk::ImageTiling,
		vk::ImageUsageFlags,
		vk::raii::Image&,
		VmaAllocation&
	) const;
	[[nodiscard]] vk::raii::ImageView
	createImageView(const vk::Image&, vk::Format, vk::ImageAspectFlags, uint32_t) const;
	void createDepthResources();

	void createBuffer(
		vk::DeviceSize,
		vk::BufferUsageFlags,
		VmaAllocationCreateFlags,
		vk::raii::Buffer&,
		VmaAllocation&,
		vk::DeviceSize = 0
	) const;

	void createCommandBuffers();

	void createSyncObjects();

	std::unique_ptr<vk::raii::CommandBuffer>
	beginSingleTimeCommands() const; // create and start a command buffer
									 // for commands to be executed only
									 // once
	void endSingleTimeCommands(
		const vk::raii::CommandBuffer&
	) const; // end command buffer once commands have been recorded, then
			 // submit commands to queue

	void transitionImageLayout(
		const vk::raii::Image&, const vk::ImageLayout, const vk::ImageLayout, uint32_t
	) const;
	void
	copyBufferToImage(const vk::raii::Buffer&, const vk::raii::Image&, uint32_t, uint32_t) const;
	void createTextureImages(const fastgltf::Asset&);

	void createTextureSamplers(const fastgltf::Asset&);

	void
	copyBuffer(vk::raii::Buffer& srcBuffer, vk::raii::Buffer& dstBuffer, vk::DeviceSize size) const;
	void loadMeshes(const fastgltf::Asset&);

	void createAccelerationStructures();

	void loadMaterials(const fastgltf::Asset&);

	void createBuffers(const fastgltf::Asset&);

	void createDescriptorPool(uint32_t);
	vk::raii::DescriptorSetLayout createDescriptorSetLayout(uint32_t) const;
	void createDescriptorSets(const fastgltf::Asset&);

	static std::vector<char> readFile(const std::string&);
	[[nodiscard]] vk::raii::ShaderModule createShaderModule(const std::vector<char>&) const;
	vk::Format findSupportedFormat(
		const std::vector<vk::Format>&, vk::ImageTiling, vk::FormatFeatureFlags
	) const;
	[[nodiscard]] vk::Format findDepthFormat() const;
	void createGraphicsPipeline();

	void transition_image_layout(
		vk::Image,
		vk::ImageLayout,
		vk::ImageLayout,
		vk::AccessFlags2,
		vk::AccessFlags2,
		vk::PipelineStageFlags2,
		vk::PipelineStageFlags2,
		vk::ImageAspectFlags
	);
	void updateTlas();
	void recreateSwapChain();
};

#endif // !VULKANINTERFACE_HPP
