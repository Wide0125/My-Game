#ifndef VULKANINTERFACE_HPP
#define VULKANINTERFACE_HPP

#include <cstdint>
#include <vector>
#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

#include "vk_mem_alloc.h"

#include <GLFW/glfw3.h>

#include <glm/glm.hpp>

#include "fastgltf/types.hpp"

#include "MeshBuffers.hpp"
#include "ModelInstance.hpp"

class Scene;

struct UniformBufferObject {
	alignas(16) glm::mat4 model;
	alignas(16) glm::mat4 view;
	alignas(16) glm::mat4 proj;
};

struct PushConstants {
	uint32_t matrixIndex;
	uint32_t textureIndex;
};

class VulkanInterface {
  public:
	VulkanInterface();
	~VulkanInterface() { cleanup(); }
	void
	loadScene(const fastgltf::Asset& asset, Scene* scene); // load gltf information onto GPU

	void waitIdle() { m_device.waitIdle(); }

	const MeshBuffers& getMeshBuffers(size_t index) const { return m_meshes[index]; }

	void drawFrame();

	GLFWwindow* const getWindow() const { return m_window; }

	int getModelInstanceCount() const {
		assert(m_currentScene != nullptr);
		return m_mvpBuffers[0].size();
	}

  private:
	static constexpr int MAX_FRAMES_IN_FLIGHT {2};

	static constexpr int WIDTH {800};
	static constexpr int HEIGHT {600};

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
	std::vector<const char*> m_requiredDeviceExtensions {vk::KHRSwapchainExtensionName};

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

	std::vector<MeshBuffers> m_meshes {};
	void destroyMeshes() {
		for (auto& meshBuffers: m_meshes) {
			vmaDestroyBuffer(
				m_allocator, meshBuffers.vertexBuffer.release(), meshBuffers.vertexAllocation
			);
			for (size_t i {0}; i < meshBuffers.indexBuffers.size(); ++i) {
				vmaDestroyBuffer(
					m_allocator,
					meshBuffers.indexBuffers[i].release(),
					meshBuffers.indexAllocations[i]
				);
			}
		}
		m_meshes.clear();
	}

	std::vector<vk::raii::Semaphore> m_presentCompleteSemaphores {};
	std::vector<vk::raii::Semaphore> m_renderFinishedSemaphores {};
	std::vector<vk::raii::Fence> m_inFlightFences {};

	std::vector<vk::raii::Image> m_textureImages {};
	std::vector<vk::raii::DeviceMemory> m_textureImageMemories {};
	std::vector<vk::raii::ImageView> m_textureImageViews {};
	std::vector<vk::raii::Sampler> m_textureSamplers {};

	std::array<std::vector<vk::raii::Buffer>, 2> m_mvpBuffers {};
	std::array<std::vector<VmaAllocation>, 2> m_mvpAllocations {};

	vk::raii::DescriptorSetLayout m_descriptorSetLayout {nullptr};
	vk::raii::DescriptorPool m_descriptorPool {nullptr};
	std::vector<vk::raii::DescriptorSet> m_descriptorSets {};

	vk::raii::Pipeline m_graphicsPipeline {nullptr};
	vk::raii::PipelineLayout m_pipelineLayout {nullptr};

	uint32_t m_frameIndex {0};

	void initWindow(); // initialize GLFW window for Vulkan
	void initVulkan(); // initialize Vulkan
	void cleanup() {
		m_textureImages.clear();
		vmaDestroyImage(m_allocator, m_depthImage.release(), m_depthImageAllocation);
		destroyMeshes();
		for (int i {0}; i < m_mvpBuffers.size(); ++i) {
			for (int j {0}; j < m_mvpBuffers[0].size(); ++j) {
				vmaDestroyBuffer(m_allocator, m_mvpBuffers[i][j].release(), m_mvpAllocations[i][j]);
			}
		}
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
		VmaAllocationCreateFlagBits,
		vk::raii::Buffer&,
		VmaAllocation&
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
	void loadModels(const fastgltf::Asset&);

	void createUniformBuffers(const fastgltf::Asset&);

	void createDescriptorPool(uint32_t, uint32_t);
	vk::raii::DescriptorSetLayout createDescriptorSetLayout(uint32_t, uint32_t) const;
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
	void recreateSwapChain();
	void updateUniformBuffer(const ModelInstance&, const glm::mat4&);
	void queueDrawModelInstance(const ModelInstance&, const glm::mat4&, uint32_t&);
};

#endif // !VULKANINTERFACE_HPP
