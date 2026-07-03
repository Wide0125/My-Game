#ifndef SCENE_HPP
#define SCENE_HPP

#include <bsm/audit.h>
#include <cassert>

#include <filesystem>
#include <format>
#include <glm/gtc/quaternion.hpp>
#include <stdexcept>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

#include <fastgltf/core.hpp>
#include <fastgltf/types.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/glm_element_traits.hpp>

#include "model.hpp"
#include "VulkanInterface.hpp"

class Scene {
    public:
        Scene(const std::string& filename, VulkanInterface& renderer) {
            static fastgltf::Parser parser {fastgltf::Extensions::KHR_texture_basisu};

            std::filesystem::path path {std::string{SCENE_PATH} + "/" + filename};

            auto data {fastgltf::GltfDataBuffer::FromPath(path)};
            if (data.error() != fastgltf::Error::None) {throw std::runtime_error("Failed to open gltf file!");}

            auto asset {parser.loadGltf(data.get(), path.parent_path(), fastgltf::Options::LoadExternalBuffers | fastgltf::Options::LoadExternalImages)};
            if(asset.error() != fastgltf::Error::None) {throw std::runtime_error(std::format("Failed to parse gltf file: {}", fastgltf::getErrorMessage(asset.error())));}

            renderer.loadScene(asset.get()); // load textures and models onto GPU memory
        }
    private:
        std::vector<ModelInstance> m_modelInstances {};
};

#endif // !SCENE_HPP
