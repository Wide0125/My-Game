#ifndef VULKANINTERFACE_HPP
#define VULKANINTERFACE_HPP

#include <cstdint>
#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif
#define VULKAN_HPP_RAII_ENABLE_DEFAULT_CONSTRUCTORS

#include "vk_mem_alloc.h"

#include <GLFW/glfw3.h>

#include <glm/glm.hpp>

#include "fastgltf/types.hpp"

class Scene;

struct ModelTransformBufferObject {
	glm::mat4 modelTransform {};
	glm::mat3 normalMatrix {};
};

struct DrawCallBufferObject {
	glm::vec3 cameraPosition {};
	glm::mat4 viewTransform {};
	glm::mat4 projectionTransform {};
	int lightCount {};
};

struct SubMeshMetadataBufferObject {
	uint32_t materialIndex {};
	enum MeshType { NORMAL, PROBE };
	MeshType type {NORMAL};
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

	enum AlphaMode { OPAQUE, MASK, BLEND };
	AlphaMode alphaMode {OPAQUE};
	float alphaCutoff {0.5};
};

struct tlasLutBufferObject {
	uint32_t materialIndex {};
	uint32_t indexStart {};
	uint32_t vertexStart {};
};

struct DrawIndirectCommand {
	uint32_t indexCount {};
	uint32_t instanceCount {};
	uint32_t firstIndex {};
	int32_t vertexOffset {};
	uint32_t firstInstance {};
};

class VulkanInterface {
  public:
	VulkanInterface();
	~VulkanInterface() { cleanup(); }
	void loadScene(const fastgltf::Asset& asset, Scene* scene); // load gltf information onto GPU

	void waitIdle() { m_device.waitIdle(); }

	void drawFrame();

	GLFWwindow* const getWindow() const { return m_window; }

	static constexpr int MAX_FRAMES_IN_FLIGHT {2};
	static constexpr int DDGI_LEVELS {1};
	static constexpr glm::ivec3 DDGI_PROBE_DIMENSIONS {32, 32, 4}; // x, y, z

	static constexpr int DDGI_PROBE_SAMPLES {192};

	static constexpr int DDGI_MODEL_INDEX {1};

#ifdef NDEBUG
	bool m_drawProbes {false};
#else
	bool m_drawDdgiProbes {true};
#endif
  private:
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
	std::vector<const char*> m_requiredDeviceExtensions {
		vk::KHRSwapchainExtensionName,
		vk::KHRAccelerationStructureExtensionName,
		vk::KHRRayQueryExtensionName,
		vk::KHRRayTracingPipelineExtensionName,
		vk::KHRDeferredHostOperationsExtensionName,
		vk::KHRBufferDeviceAddressExtensionName
	};
	vk::raii::Device m_device {nullptr}; // interface to interact with GPU

	uint32_t m_queueFamilyIndex {~0u}; // index of selected queue family
	vk::raii::Queue m_queue {nullptr}; // selected queue

	VmaAllocator m_allocator {};

	vk::raii::SwapchainKHR m_swapChain {nullptr};
	std::vector<vk::Image> m_swapChainImages {};
	vk::Extent2D m_swapChainExtent {};
	vk::SurfaceFormatKHR m_swapChainSurfaceFormat {};
	std::vector<vk::raii::ImageView> m_swapChainImageViews {};

	vk::raii::CommandPool m_commandPool {nullptr};
	std::vector<vk::raii::CommandBuffer> m_commandBuffers {};

	std::array<vk::Buffer, MAX_FRAMES_IN_FLIGHT> m_drawCommandsBuffers {};
	std::array<VmaAllocation, MAX_FRAMES_IN_FLIGHT> m_drawCommandsAllocations {};

	std::array<vk::Buffer, MAX_FRAMES_IN_FLIGHT> m_metadataBuffers {};
	std::array<VmaAllocation, MAX_FRAMES_IN_FLIGHT> m_metadataAllocations {};
	uint32_t m_opaqueDrawCallsCount {};
	uint32_t m_transparentDrawCallsCount {};

	vk::Image m_depthImage {nullptr};
	VmaAllocation m_depthImageAllocation {nullptr};
	vk::raii::ImageView m_depthImageView {nullptr};

	vk::Buffer m_vertexBuffer {nullptr};
	VmaAllocation m_vertexAllocation {};
	uint32_t m_vertexCount {};
	vk::Buffer m_indexBuffer {nullptr};
	VmaAllocation m_indexAllocation {};
	uint32_t m_indexCount {};
	struct Mesh {
		struct SubMesh {
			uint32_t indexStart {};
			uint32_t indexCount {};
			uint32_t maxIndex {};
			uint32_t vertexOffset {};
			uint32_t materialIndex {};
			MaterialBufferObject::AlphaMode alphaMode {MaterialBufferObject::OPAQUE};
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

	vk::Buffer m_materialBuffer {nullptr};
	VmaAllocation m_materialAllocation {};

	std::array<vk::Buffer, MAX_FRAMES_IN_FLIGHT> m_modelTransformBuffers {

	};
	std::array<VmaAllocation, MAX_FRAMES_IN_FLIGHT> m_modelTransformAllocations {};

	std::array<vk::Buffer, MAX_FRAMES_IN_FLIGHT> m_vpTransformBuffers {

	};
	std::array<VmaAllocation, MAX_FRAMES_IN_FLIGHT> m_vpTransformAllocations {};

	std::array<vk::Buffer, MAX_FRAMES_IN_FLIGHT> m_lightBuffers {};
	std::array<VmaAllocation, MAX_FRAMES_IN_FLIGHT> m_lightAllocations {};

	vk::raii::DescriptorSetLayout m_descriptorSetLayout {nullptr};
	vk::raii::DescriptorPool m_descriptorPool {nullptr};
	std::vector<vk::raii::DescriptorSet> m_descriptorSets {};

	vk::raii::Pipeline m_graphicsPipeline {nullptr};
	vk::raii::PipelineLayout m_graphicsPipelineLayout {nullptr};

	vk::raii::Pipeline m_computeRaySamplePipeline {nullptr};
	vk::raii::PipelineLayout m_computeRaySamplePipelineLayout {nullptr};
	vk::raii::Pipeline m_computeProbeIrradianceUpdatePipeline {nullptr};
	vk::raii::PipelineLayout m_computeProbeIrradianceUpdatePipelineLayout {nullptr};
	vk::raii::Pipeline m_computeProbeDepthUpdatePipeline {nullptr};
	vk::raii::PipelineLayout m_computeProbeDepthUpdatePipelineLayout {nullptr};

	std::vector<vk::Buffer> m_blasBuffers {};
	std::vector<VmaAllocation> m_blasAllocations {};
	std::vector<vk::raii::AccelerationStructureKHR> m_blasHandles {};

	std::vector<vk::AccelerationStructureInstanceKHR> m_blasInstances {};
	vk::Buffer m_blasInstanceBuffer {nullptr};
	VmaAllocation m_blasInstanceAllocation {};

	vk::Buffer m_tlasBuffer {nullptr};
	VmaAllocation m_tlasAllocation {nullptr};
	vk::Buffer m_tlasScratchBuffer {nullptr};
	VmaAllocation m_tlasScratchAllocation {nullptr};
	vk::raii::AccelerationStructureKHR m_tlas {nullptr};

	vk::Buffer m_tlasLutBuffer {nullptr};
	VmaAllocation m_tlasLutAllocation {};
	uint32_t m_tlasLutCount {};

	vk::DeviceSize m_accelerationStructureScratchOffset {};

	uint32_t m_frameIndex {0};

	std::vector<glm::vec3> m_ddgiProbePositions {};
	std::array<vk::Buffer, MAX_FRAMES_IN_FLIGHT> m_ddgiProbePositionBuffers {};
	std::array<VmaAllocation, MAX_FRAMES_IN_FLIGHT> m_ddgiProbePositionAllocations {};

	vk::Image m_ddgiIrradianceImage {};
	VmaAllocation m_ddgiIrradianceAllocation {};
	vk::raii::ImageView m_ddgiIrradianceImageView {nullptr};

	vk::Image m_ddgiDepthImage {};
	VmaAllocation m_ddgiDepthAllocation {};
	vk::raii::ImageView m_ddgiDepthImageView {nullptr};

	std::array<vk::Buffer, MAX_FRAMES_IN_FLIGHT> m_ddgiProbeSampleBuffers {};
	std::array<VmaAllocation, MAX_FRAMES_IN_FLIGHT> m_ddgiProbeSampleAllocations {};

	std::array<vk::Buffer, MAX_FRAMES_IN_FLIGHT> m_ddgiProbeBoundsBuffers {};
	std::array<VmaAllocation, MAX_FRAMES_IN_FLIGHT> m_ddgiProbeBoundsAllocations {};

	vk::raii::Sampler m_ddgiTextureSampler {nullptr};

	vk::Image m_ddgiRadianceTransmissionImage {};
	VmaAllocation m_ddgiRadianceTransmissionAllocation {};
	vk::raii::ImageView m_ddgiRadianceTransmissionImageView {nullptr};

	vk::Image m_ddgiDistanceTransmissionImage {};
	VmaAllocation m_ddgiDistanceTransmissionAllocation {};
	vk::raii::ImageView m_ddgiDistanceTransmissionImageView {nullptr};

	vk::Image m_ddgiDepthSampleCountImage {};
	VmaAllocation m_ddgiDepthSampleCountAllocation {};
	vk::raii::ImageView m_ddgiDepthSampleCountImageView {nullptr};

	vk::ImageSubresourceRange m_ddgiTexturesClearRange {};

	vk::MemoryBarrier2 m_pipelineMemoryBarrier {};	// reusable memory barrier
	vk::DependencyInfo m_pipelineDependencyInfo {}; // reusable dependency info

	void initWindow(); // initialize GLFW window for Vulkan
	void initVulkan(); // initialize Vulkan
	void cleanup();

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
		uint32_t width,
		uint32_t height,
		uint32_t mipLevels,
		vk::Format format,
		vk::ImageTiling tiling,
		vk::ImageUsageFlags usage,
		vk::Image& image,
		VmaAllocation& allocation
	) const;
	void transitionImageLayout(
		const vk::Image& image,
		const vk::ImageLayout oldLayout,
		const vk::ImageLayout newLayout,
		uint32_t mipLevels
	);
	[[nodiscard]] vk::raii::ImageView createImageView(
		const vk::Image& image,
		vk::Format format,
		vk::ImageAspectFlags aspectFlags,
		uint32_t mipLevels
	) const;
	void createDepthResources();

	void createBuffer(
		vk::DeviceSize,
		vk::BufferUsageFlags,
		VmaAllocationCreateFlags,
		vk::Buffer&,
		VmaAllocation&,
		vk::DeviceSize = 0
	) const;
	void createGPUBufferWithData(
		vk::DeviceSize bufferSize,
		vk::BufferUsageFlags usageFlags,
		const void* data,
		vk::Buffer& buffer,
		VmaAllocation& allocation,
		vk::DeviceSize minAlignment = 0,
		vk::DeviceSize dataSize = 0
	) const;
	void createHostBufferWithData(
		vk::DeviceSize bufferSize,
		vk::BufferUsageFlags usageFlags,
		const void* data,
		vk::Buffer& buffer,
		VmaAllocation& allocation,
		vk::DeviceSize minAlignment = 0,
		vk::DeviceSize dataSize = 0
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

	void copyBufferToImage(const vk::Buffer&, const vk::raii::Image&, uint32_t, uint32_t) const;
	void createTextureImages(const fastgltf::Asset&);

	void createTextureSamplers(const fastgltf::Asset&);

	void createDdgiProbes();

	void copyBuffer(vk::Buffer& srcBuffer, vk::Buffer& dstBuffer, vk::DeviceSize size) const;
	void loadMeshes(const fastgltf::Asset&);

	void createAccelerationStructures();

	void loadMaterials(const fastgltf::Asset&);

	void createBuffers();

	vk::DescriptorPoolCreateInfo createDescriptorPool(uint32_t) const;
	vk::raii::DescriptorSetLayout createDescriptorSetLayout(uint32_t) const;
	void createDescriptorSets(const fastgltf::Asset&);
	void createComputeDescriptorSets();

	static std::vector<char> readFile(const std::string&);
	[[nodiscard]] vk::raii::ShaderModule createShaderModule(const std::vector<char>&) const;
	vk::Format findSupportedFormat(
		const std::vector<vk::Format>&, vk::ImageTiling, vk::FormatFeatureFlags
	) const;
	[[nodiscard]] vk::Format findDepthFormat() const;
	void clearImage(vk::Image& image, vk::ImageLayout imageLayout);
	void computeTangents(
		const std::vector<glm::vec3>& positions,
		const std::vector<glm::vec3>& normals,
		const std::vector<glm::vec2>& texCoords,
		const std::vector<uint32_t>& indices,
		int indexStart,
		int indexCount,
		int vertexOffset,
		std::vector<glm::vec4>& tangentReturn
	) const;

	void createGraphicsPipeline();
	void createComputePipelines();

	void transitionImageLayoutPipeline(
		vk::Image,
		vk::ImageLayout,
		vk::ImageLayout,
		vk::AccessFlags2,
		vk::AccessFlags2,
		vk::PipelineStageFlags2,
		vk::PipelineStageFlags2,
		vk::ImageAspectFlags
	);
	void updateBuffers();
	void updateTlas();
	void restructureDdgiProbes();
	size_t probeCoordinatesToIndex(int, int, int) const;
	void recreateSwapChain();
	std::vector<glm::vec3> distributePointsOnUnitSphere(int samples) const;
};

#endif // !VULKANINTERFACE_HPP
