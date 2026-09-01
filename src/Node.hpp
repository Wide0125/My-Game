#ifndef MODELINSTANCE_HPP
#define MODELINSTANCE_HPP

#include <optional>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/transform.hpp>

struct Node {
	glm::vec3 position {};
	glm::quat rotation {};
	glm::vec3 scale {};
	std::vector<size_t> childIndices {}; // required values

	std::optional<size_t> parentIndex {};
	std::optional<size_t> modelInstanceIndex {};
	std::optional<size_t> lightIndex {};

	bool hasLightChild {false};
	bool hasModelInstanceChild {false};

	glm::mat4 getTransform() const {
		return {glm::translate(position) * glm::toMat4(rotation) * glm::scale(scale)};
	}
};

#endif // !MODELINSTANCE_HPP
