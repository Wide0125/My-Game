#ifndef VULKANINTERFACE_HPP
#define VULKANINTERFACE_HPP

#include "vulkan/vulkan.hpp"
#include <cstdint>
#include <vector>
#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

class VulkanInterface {
    public:
        VulkanInterface();
    private:
        GLFWwindow* m_window {nullptr}; // GLFW window object

        vk::raii::Context m_context {};
        vk::raii::Instance m_instance {nullptr}; // Vulkan instance

        vk::raii::SurfaceKHR m_surface {nullptr}; // surface for Vulkan to draw onto

        std::vector<vk::raii::PhysicalDevice> m_availablePhysicalDevices {};
        vk::raii::PhysicalDevice m_physicalDevice {nullptr}; // object representation of selected physical GPU
        vk::raii::Device m_device {nullptr}; // interface to interact with GPU
        uint32_t m_queueFamilyIndex {~0u}; // index of selected queue family
        vk::raii::Queue m_queue {nullptr}; // selected queue
        std::vector<const char*> m_requiredDeviceExtensions {
            vk::KHRSwapchainExtensionName
        };
 
        vk::raii::SwapchainKHR m_swapChain {nullptr};
        std::vector<vk::Image> m_swapChainImages {};
        vk::Extent2D m_swapChainExtent {};
        vk::SurfaceFormatKHR m_swapChainSurfaceFormat {};
        std::vector<vk::raii::ImageView> m_swapChainImageViews {};

        vk::raii::DescriptorSetLayout m_descriptorSetLayout {nullptr};
        vk::raii::Pipeline m_graphicsPipeline {nullptr};
        vk::raii::PipelineLayout m_pipelineLayout {nullptr};

        vk::raii::CommandPool m_commandPool {nullptr};

        vk::raii::Image m_depthImage {nullptr};
        vk::raii::DeviceMemory m_depthImageMemory {nullptr};
        vk::raii::ImageView m_depthImageView {nullptr};

        vk::raii::Buffer vertexBuffer {nullptr};
        vk::raii::DeviceMemory vertexBufferMemory {nullptr};
        vk::raii::Buffer indexBuffer {nullptr};
        vk::raii::DeviceMemory indexBufferMemory {nullptr};


        void initWindow(); // intialize GLFW window for Vulkan
        void initVulkan(); // initialize Vulkan

        void createInstance(); // initialize Vulkan
        void createSurface(); // create surface for Vulkan to draw on, linked to the created GLFW window

        bool isDeviceSuitable(const vk::raii::PhysicalDevice&) const;
        void pickPhysicalDevice(); // pick physical GPU to run Vulkan on
        void createLogicalDevice(); // create logical device to interface with physical GPU

        vk::Extent2D chooseSwapExtent(const vk::SurfaceCapabilitiesKHR&) const; // choose extent(size) of swap chain
        static uint32_t chooseSwapMinImageCount(const vk::SurfaceCapabilitiesKHR&);
        static vk::SurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>&);
        void createSwapChain(); // create swapchain of surfaces
        void createSwapChainImageViews(); // create image views of swap chain images

        void createDescriptorSetLayout();

        static std::vector<char> readFile(const std::string&);
        [[nodiscard]] vk::raii::ShaderModule createShaderModule(const std::vector<char>&) const;
        vk::Format findSupportedFormat(const std::vector<vk::Format>&, vk::ImageTiling, vk::FormatFeatureFlags) const;
        [[nodiscard]] vk::Format findDepthFormat() const;
        void createGraphicsPipeline();

        void createCommandPool();

        uint32_t findMemoryType(uint32_t, vk::MemoryPropertyFlags) const;
        void createImage(uint32_t, uint32_t, uint32_t, vk::Format, vk::ImageTiling, vk::ImageUsageFlags, vk::MemoryPropertyFlags, vk::raii::Image&, vk::raii::DeviceMemory&) const;
        [[nodiscard]] vk::raii::ImageView createImageView(const vk::raii::Image&, vk::Format, vk::ImageAspectFlags, uint32_t) const;
        void createDepthResources();

        void createVertexBuffer();
        void createIndexBuffer();
};

#endif // !VULKANINTERFACE_HPP
