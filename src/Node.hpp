#ifndef MODELINSTANCE_HPP
#define MODELINSTANCE_HPP

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/gtx/quaternion.hpp>

#include "MeshBuffers.hpp"

struct Node {
	Node(glm::vec3 position, glm::quat rotation, glm::vec3 scale, std::vector<size_t> childIndices)
		: position {position}, rotation {rotation}, scale {scale}, childIndices {childIndices} {}
	virtual ~Node() {};

	glm::vec3 position {};
	glm::quat rotation {};
	glm::vec3 scale {};
	std::vector<size_t> childIndices {};
};

struct ModelInstance : Node {
	ModelInstance(const Node& node, const MeshBuffers* const mesh) : Node {node}, mesh {mesh} {}
	const MeshBuffers* mesh {};
};

struct Light : Node {
	enum LightType { directional, point, spot };
	Light(
		const Node& node,
		LightType type,
		glm::vec3 color,
		float intensity,
		float range,
		std::string_view name
	)
		: Node {node}, type {type}, color {color}, intensity {intensity}, range {range}, name {name} {}
	LightType type {};
	glm::vec3 color {};
	float intensity {};
	float range {};
	std::string name {};
};

#endif // !MODELINSTANCE_HPP
