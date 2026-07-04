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

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "fastgltf/types.hpp"
#include "vertex.hpp"

struct UniformBufferObject {
	alignas(16) glm::mat4 model;
	alignas(16) glm::mat4 view;
	alignas(16) glm::mat4 proj;
};

class VulkanInterface {
  public:
	VulkanInterface();
	~VulkanInterface() { cleanup(); }
	void loadScene(const fastgltf::Asset& asset); // load gltf information onto GPU

	void waitIdle() { m_device.waitIdle(); }

  private:
	static constexpr int MAX_FRAMES_IN_FLIGHT {2};

	GLFWwindow* m_window {nullptr}; // GLFW window object

	vk::raii::Context m_context {};
	vk::raii::Instance m_instance {nullptr}; // Vulkan instance

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

	vk::raii::DescriptorSetLayout m_descriptorSetLayout {nullptr};
	vk::raii::Pipeline m_graphicsPipeline {nullptr};
	vk::raii::PipelineLayout m_pipelineLayout {nullptr};

	vk::raii::CommandPool m_commandPool {nullptr};
	std::vector<vk::raii::CommandBuffer> m_commandBuffers {};

	vk::raii::Image m_depthImage {nullptr};
	VmaAllocation m_depthImageAllocation {nullptr};
	vk::raii::ImageView m_depthImageView {nullptr};

	vk::raii::Buffer m_vertexBuffer {nullptr};
	vk::raii::DeviceMemory m_vertexBufferMemory {nullptr};
	vk::raii::Buffer m_indexBuffer {nullptr};
	vk::raii::DeviceMemory m_indexBufferMemory {nullptr};

	std::vector<vk::raii::Semaphore> m_presentCompleteSemaphores {};
	std::vector<vk::raii::Semaphore> m_renderFinishedSemaphores {};
	std::vector<vk::raii::Fence> m_inFlightFences {};

	std::vector<vk::raii::Image> m_textureImages {};
	std::vector<vk::raii::DeviceMemory> m_textureImageMemories {};
	std::vector<vk::raii::ImageView> m_textureImageViews {};
	std::vector<vk::raii::Sampler> m_textureSamplers {};

	void initWindow(); // initialize GLFW window for Vulkan
	void initVulkan(); // initialize Vulkan
	void cleanup() {
		m_textureImages.clear();
		vmaDestroyImage(m_allocator, m_depthImage.release(), m_depthImageAllocation);
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

	void createDescriptorSetLayout();

	static std::vector<char> readFile(const std::string&);
	[[nodiscard]] vk::raii::ShaderModule createShaderModule(const std::vector<char>&) const;
	vk::Format findSupportedFormat(
		const std::vector<vk::Format>&, vk::ImageTiling, vk::FormatFeatureFlags
	) const;
	[[nodiscard]] vk::Format findDepthFormat() const;
	void createGraphicsPipeline();

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
	createImageView(const vk::raii::Image&, vk::Format, vk::ImageAspectFlags, uint32_t) const;
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

	void loadModels(const fastgltf::Asset&);
};

#endif // !VULKANINTERFACE_HPP
