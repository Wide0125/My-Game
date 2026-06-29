#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION

#include "VulkanInterface.hpp"
#include "scene.hpp"


int main() {
    VulkanInterface renderer {};
    std::unique_ptr<Scene> currentScene {std::make_unique<Scene>("Scene1.gltf")};
    return 0;
}
