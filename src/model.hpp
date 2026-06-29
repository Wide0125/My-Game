#ifndef MODEL_HPP
#define MODEL_HPP
#include "vertex.hpp"
#include <glm/gtc/quaternion.hpp>

class Model {
    public:
    private:
        std::vector<Vertex> m_vertices {};
        std::vector<uint32_t> m_indices{};
};

class ModelInstance {
    public:
        ModelInstance(const Model& model, const glm::vec3& position, const glm::quat& rotation, float scale)
            : m_model {model}, m_position {position}, m_rotation {rotation}, m_scale {scale}
        {}
    private:
        const Model& m_model {};
        glm::vec3 m_position {};
        glm::quat m_rotation {};
        float m_scale {};
};


#endif // !MODEL_HPP
