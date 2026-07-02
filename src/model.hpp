#ifndef MODEL_HPP
#define MODEL_HPP
#include "vertex.hpp"
#include <glm/gtc/quaternion.hpp>

struct ModelInstance {
    int modelIndex {};
    glm::vec3 position {};
    glm::quat rotation {};
    float scale {};
};


#endif // !MODEL_HPP
