#ifndef SCENE_HPP
#define SCENE_HPP

#include <cassert>
#include <filesystem>
#include <format>
#include <queue>
#include <stdexcept>
#include <print>

#include <glm/gtc/quaternion.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/transform.hpp>

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

		m_nodes.reserve(asset->nodes.size());
		m_modelInstancesPerMesh.resize(asset->meshes.size());
		m_modelInstanceTransforms.reserve(
			std::ranges::fold_left(m_modelInstancesPerMesh, 0, std::plus {})
		);
		auto flattenQueue {getNodes(asset.get())};
		for (size_t meshIndex {0}; meshIndex < flattenQueue.size(); ++meshIndex) {
			for (auto NodeIndex: flattenQueue[meshIndex]) {
				m_modelInstanceTransforms.push_back({});
				m_nodes[NodeIndex].modelInstanceIndex =
					m_modelInstanceTransforms.size() - 1;
			}
		}

		int modelInstanceRunningCount {0};
		m_transparentModelInstances.reserve(m_modelInstanceTransforms.size());
		for (int meshIndex {0}; meshIndex < asset->meshes.size(); ++meshIndex) {
			std::vector<size_t> transparentSubMeshIndices {};
			for (
				int subMeshIndex {0}; subMeshIndex < asset->meshes[meshIndex].primitives.size();
				++subMeshIndex
			) {
				if (asset
						->materials[asset->meshes[meshIndex]
										.primitives[subMeshIndex]
										.materialIndex.value()]
						.alphaMode == fastgltf::AlphaMode::Blend) {
					transparentSubMeshIndices.push_back(subMeshIndex);
				}
			}
			for (
				int modelInstanceIndex {modelInstanceRunningCount};
				modelInstanceIndex < modelInstanceRunningCount + m_modelInstancesPerMesh[meshIndex];
				++modelInstanceIndex
			) {
				m_transparentModelInstances.push_back(
					{modelInstanceIndex, meshIndex, transparentSubMeshIndices}
				);
			}
			modelInstanceRunningCount += m_modelInstancesPerMesh[meshIndex];
		}

		m_parentNodes.reserve(asset->scenes[0].nodeIndices.size());
		for (const auto& parentNodeIndex: asset->scenes[0].nodeIndices) {
			m_parentNodes.push_back(parentNodeIndex);
			traverseTreeInitial(parentNodeIndex, glm::identity<glm::mat4>());
		}

		renderer.loadScene(asset.get(), this); // load textures and models onto GPU memory
	}

	const std::vector<size_t>& getParentNodeIndices() const { return m_parentNodes; }
	const std::vector<Node>& getNodes() const { return m_nodes; }

	const std::vector<ModelTransformBufferObject>& getModelInstanceTransforms() const {
		return m_modelInstanceTransforms;
	}
	const std::vector<size_t>& getModelInstancesPerMesh() { return m_modelInstancesPerMesh; }
	const std::vector<std::tuple<size_t, size_t, std::vector<size_t>>>&
	getTransparentModelInstance() {
		return m_transparentModelInstances;
	}

	const std::vector<LightBufferObject>& getLights() const { return m_lights; }

	const glm::vec3& getCameraPosition() { return m_sceneCamera.getCameraPosition(); }
	const glm::vec3 getLookAtVector() { return m_sceneCamera.getLookAtVector(); }
	const glm::vec3 getUp() { return m_sceneCamera.getUp(); }
	void moveCameraPosition(const glm::vec3& delta) {
		m_sceneCamera.moveCameraPosition(delta);
		m_cameraMoved = true;
	}
	void moveCameraGaze(const glm::vec2& delta) {
		m_sceneCamera.moveCameraGaze(delta);
		m_cameraMoved = true;
	}

	void propagateUpdates() { // check for any updates and propagate accordingly
		while (!m_updates.empty()) {
			size_t currUpdateIndex {m_updates.front()};
			m_updates.pop();

			Node& currUpdateNode {m_nodes[currUpdateIndex]};

			if (currUpdateNode.modelInstanceIndex.has_value() or
				currUpdateNode.lightIndex.has_value() or currUpdateNode.hasModelInstanceChild or
				currUpdateNode.hasLightChild) {
				Node& currNode {currUpdateNode};
				glm::mat4 cumulativeTransform {
					currUpdateNode.getTransform()
				}; // travel upwards towards top in a straight line, accumulating transform matrices
				while (currNode.parentIndex.has_value()) {
					currNode = m_nodes[currNode.parentIndex.value()];
					cumulativeTransform = currNode.getTransform() * cumulativeTransform;
				}
				if (currUpdateNode.modelInstanceIndex.has_value()) {
					m_modelInstanceTransforms[currUpdateNode.modelInstanceIndex.value()]
						.modelTransform = cumulativeTransform;
					m_modelInstanceTransforms[currUpdateNode.modelInstanceIndex.value()]
						.normalMatrix =
						glm::transpose(glm::inverse(glm::mat3(cumulativeTransform)));
				}
				if (currUpdateNode.lightIndex.has_value()) {
					m_lights[currUpdateNode.lightIndex.value()].position = {
						cumulativeTransform[3][0],
						cumulativeTransform[3][1],
						cumulativeTransform[3][2]
					};
					m_lights[currUpdateNode.lightIndex.value()].direction =
						glm::vec3 {cumulativeTransform * glm::vec4 {0, 0, 1, 0}};
				}
				if (currUpdateNode.hasModelInstanceChild) {
					for (const auto& childNodeIndex: currUpdateNode.childIndices) {
						recursiveUpdateChildrenModelInstances(
							m_nodes[childNodeIndex], cumulativeTransform
						);
					}
				}
				if (currUpdateNode.hasLightChild) {
					for (const auto& childNodeIndex: currUpdateNode.childIndices) {
						recursiveUpdateChildrenLights(m_nodes[childNodeIndex], cumulativeTransform);
					}
				}
			}
		}
	}

	void sortTransparentObjects() {
		if (m_cameraMoved) {
			std::ranges::sort(
				m_transparentModelInstances,
				[&](const std::tuple<size_t, size_t, std::vector<size_t>>& first,
					const std::tuple<size_t, size_t, std::vector<size_t>>& second) {
					glm::vec3 firstLocation {
						m_modelInstanceTransforms[std::get<0>(first)].modelTransform[3]
					};
					glm::vec3 secondLocation {
						m_modelInstanceTransforms[std::get<0>(second)].modelTransform[3]
					};
					auto distance1 {
						glm::distance(firstLocation, m_sceneCamera.getCameraPosition())
					};
					auto distance2 {
						glm::distance(secondLocation, m_sceneCamera.getCameraPosition())
					};

					return distance1 > distance2;
				}
			);
			m_cameraMoved = false;
		}
	}

  private:
	std::vector<size_t> m_parentNodes {};
	std::vector<Node> m_nodes {};

	std::vector<ModelTransformBufferObject> m_modelInstanceTransforms {};
	std::vector<size_t> m_modelInstancesPerMesh {};
	std::vector<std::tuple<size_t, size_t, std::vector<size_t>>>
		m_transparentModelInstances {}; // modelInstance index, mesh index, subMesh index

	std::vector<LightBufferObject> m_lights {};

	std::queue<size_t> m_updates {};

	Camera m_sceneCamera {};
	bool m_cameraMoved {false};

	std::vector<std::vector<size_t>> getNodes(const fastgltf::Asset& asset) {
		std::vector<std::vector<size_t>> flattenQueue {};
		flattenQueue.resize(asset.meshes.size());
		for (const auto& node: asset.nodes) {
			fastgltf::TRS TRS {std::get<fastgltf::TRS>(node.transform)};
			glm::vec3 translation {TRS.translation.x(), TRS.translation.y(), TRS.translation.z()};
			glm::quat rotation {
				TRS.rotation.w(), TRS.rotation.x(), TRS.rotation.y(), TRS.rotation.z()
			};
			glm::vec3 scale {TRS.scale.x(), TRS.scale.y(), TRS.scale.z()};
			std::vector<size_t> childIndices {node.children.begin(), node.children.end()};

			Node currNode {translation, rotation, scale, childIndices};

			if (node.meshIndex.has_value()) {
				m_modelInstancesPerMesh[node.meshIndex.value()] += 1;
				flattenQueue[node.meshIndex.value()].push_back(m_nodes.size());
			}
			if (node.lightIndex.has_value()) {
				const auto& currLight {asset.lights[node.lightIndex.value()]};
				LightBufferObject currLightBuffer {
					static_cast<LightBufferObject::LightType>(currLight.type),
					glm::vec3 {currLight.color.x(), currLight.color.y(), currLight.color.z()},
					currLight.intensity,
				};
				if (currLight.range.has_value()) {
					currLightBuffer.range = currLight.range.value();
				} else {
					currLightBuffer.range = -1;
				}
				m_lights.push_back(currLightBuffer);
				currNode.lightIndex = m_lights.size() - 1;
			}
			m_nodes.push_back(currNode);
		}
		return std::move(flattenQueue);
	}
	void traverseTreeInitial(
		size_t currIndex, const glm::mat4& globalTransform, std::optional<size_t> parentIndex = {}
	) {
		Node& node = m_nodes[currIndex];

		node.parentIndex = parentIndex;
		const glm::mat4& localTransform {node.getTransform()};
		glm::mat4 currentTransform {globalTransform * localTransform};
		if (node.modelInstanceIndex.has_value()) {
			m_modelInstanceTransforms[node.modelInstanceIndex.value()].modelTransform =
				currentTransform;
			m_modelInstanceTransforms[node.modelInstanceIndex.value()].normalMatrix =
				glm::transpose(glm::inverse(glm::mat3(currentTransform)));
			Node& currNode {m_nodes[currIndex]};
			while (
				currNode.parentIndex.has_value() and
				!m_nodes[currNode.parentIndex.value()].hasModelInstanceChild
			) {
				currNode = m_nodes[currNode.parentIndex.value()];
				currNode.hasModelInstanceChild = true;
			}
		}
		if (node.lightIndex.has_value()) {
			m_lights[node.lightIndex.value()].position = {
				currentTransform[3][0], currentTransform[3][1], currentTransform[3][2]
			};
			m_lights[node.lightIndex.value()].direction =
				glm::vec3 {currentTransform * glm::vec4 {0, 0, 1, 0}};

			Node& currNode {m_nodes[currIndex]};
			while (
				currNode.parentIndex.has_value() and
				!m_nodes[currNode.parentIndex.value()].hasLightChild
			) {
				currNode = m_nodes[currNode.parentIndex.value()];
				currNode.hasLightChild = true;
			}
		}
		for (auto childNodeIndex: node.childIndices) {
			traverseTreeInitial(childNodeIndex, currentTransform, currIndex);
		}
	}

	void recursiveUpdateChildrenModelInstances(const Node& node, const glm::mat4& globalTransform) {
		glm::mat4 currentTransform {globalTransform * node.getTransform()};
		if (node.modelInstanceIndex.has_value()) {
			m_modelInstanceTransforms[node.modelInstanceIndex.value()].modelTransform =
				currentTransform;
			m_modelInstanceTransforms[node.modelInstanceIndex.value()].normalMatrix =
				glm::transpose(glm::inverse(glm::mat3(currentTransform)));
		}
		if (node.hasModelInstanceChild) {
			for (const auto& childNodeIndex: node.childIndices) {
				recursiveUpdateChildrenModelInstances(m_nodes[childNodeIndex], currentTransform);
			}
		}
	}
	void recursiveUpdateChildrenLights(const Node& node, const glm::mat4& globalTransform) {
		glm::mat4 currentTransform {globalTransform * node.getTransform()};
		if (node.lightIndex.has_value()) {
			m_lights[node.lightIndex.value()].position = {
				currentTransform[3][0], currentTransform[3][1], currentTransform[3][2]
			};
			m_lights[node.lightIndex.value()].direction =
				glm::vec3 {currentTransform * glm::vec4 {0, 0, 1, 0}};
		}
		if (node.hasLightChild) {
			for (const auto& childNodeIndex: node.childIndices) {
				recursiveUpdateChildrenLights(m_nodes[childNodeIndex], currentTransform);
			}
		}
	}
};

#endif // !SCENE_HPP
