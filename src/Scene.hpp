#ifndef SCENE_HPP
#define SCENE_HPP

#include <cassert>
#include <filesystem>
#include <format>
#include <stdexcept>

#include <glm/gtc/quaternion.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

#include <fastgltf/core.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>

#include "Camera.hpp"
#include "Node.hpp"
#include "VulkanInterface.hpp"

class Scene {
  public:
	Scene(const std::string& filename, VulkanInterface& renderer) {
		static fastgltf::Parser parser {
			fastgltf::Extensions::KHR_texture_basisu | fastgltf::Extensions::KHR_lights_punctual
		};

		std::filesystem::path path {std::string {SCENE_PATH} + "/" + filename};

		auto data {fastgltf::GltfDataBuffer::FromPath(path)};
		if (data.error() != fastgltf::Error::None) {
			throw std::runtime_error("Failed to open gltf file!");
		}

		auto asset {parser.loadGltf(
			data.get(),
			path.parent_path(),
			fastgltf::Options::LoadExternalBuffers | fastgltf::Options::LoadExternalImages |
				fastgltf::Options::DecomposeNodeMatrices
		)};
		if (asset.error() != fastgltf::Error::None) {
			throw std::runtime_error(
				std::format(
					"Failed to parse gltf file: {}", fastgltf::getErrorMessage(asset.error())
				)
			);
		}

		renderer.loadScene(asset.get(), this); // load textures and models onto GPU memory

		m_nodes.reserve(asset->nodes.size());
		for (const auto& node: asset->nodes) {
			getNode(node, renderer);
		}
		m_parentModelInstances.reserve(asset->scenes[0].nodeIndices.size());
		for (const auto& parentNodeIndex: asset->scenes[0].nodeIndices) {
			m_parentModelInstances.push_back(m_nodes[parentNodeIndex].get());
		}
	}

	const std::vector<const Node*>& getParentNodes() const { return m_parentModelInstances; }
	const std::vector<std::unique_ptr<Node>>& getNodes() const { return m_nodes; }

	Camera sceneCamera {};

  private:
	std::vector<const Node*> m_parentModelInstances {};
	std::vector<std::unique_ptr<Node>> m_nodes {};

	void getNode(const fastgltf::Node& node, const VulkanInterface& renderer) {
		fastgltf::TRS TRS {std::get<fastgltf::TRS>(node.transform)};
		glm::vec3 translation {TRS.translation.x(), TRS.translation.y(), TRS.translation.z()};
		glm::quat rotation {TRS.rotation.w(), TRS.rotation.x(), TRS.rotation.y(), TRS.rotation.z()};
		glm::vec3 scale {TRS.scale.x(), TRS.scale.y(), TRS.scale.z()};
		std::vector<size_t> childIndices {node.children.begin(), node.children.end()};

		std::unique_ptr<Node> nodePointer {};
		if (node.meshIndex.has_value()) {
			nodePointer = std::make_unique<ModelInstance>(ModelInstance {
				{translation, rotation, scale, childIndices},
				&renderer.getMeshBuffers(node.meshIndex.value())
			});
		} else {
			nodePointer = std::make_unique<Node>(translation, rotation, scale, childIndices);
		} // TODO proper light input
		m_nodes.push_back(std::move(nodePointer));
	}
};

#endif // !SCENE_HPP
