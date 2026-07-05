#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#define TINYGLTF_IMPLEMENTATION

#define VMA_IMPLEMENTATION

#define GLFW_INCLUDE_VULKAN

#include "VulkanInterface.hpp"
#include "scene.hpp"


int main() {
    VulkanInterface renderer {};
    std::unique_ptr<Scene> currentScene {std::make_unique<Scene>("Scene1.glb", renderer)};
    renderer.waitIdle();
    return 0;
}
