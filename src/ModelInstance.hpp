#ifndef MODELINSTANCE_HPP
#define MODELINSTANCE_HPP

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/gtx/quaternion.hpp>

#include "MeshBuffers.hpp"

struct ModelInstance {
	const MeshBuffers* mesh {};
	glm::vec3 position {};
	glm::quat rotation {};
	glm::vec3 scale {};
	std::vector<size_t> childIndices {};
};

#endif // !MODELINSTANCE_HPP
