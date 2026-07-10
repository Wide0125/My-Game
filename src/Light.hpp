#ifndef LIGHT_HPP
#define LIGHT_HPP

#include <glm/glm.hpp>

struct Light {
	enum LightType { directional, point, spot };
	LightType lightType {directional};
	glm::vec3 color {1.0, 1.0, 1.0};
};

#endif // !LIGHT_HPP

