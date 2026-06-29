#ifndef SCENE_HPP
#define SCENE_HPP

#include <cassert>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include "tiny_gltf_v3.h"

#include "model.hpp"

class Scene {
    public:
        Scene(const std::string& filename) {
            tg3_parse_options opts;
            tg3_error_stack errors;
            tg3_model model;

            tg3_parse_options_init(&opts);
            tg3_error_stack_init(&errors);

            std::string path {std::string(SHADER_PATH) + '/' + filename};
            tg3_error_code err = tg3_parse_file(&model, &errors, path.c_str(), path.length(), &opts);
            if (err != TG3_OK) {
                for (uint32_t i = 0; i < errors.count; i++) {
                    fprintf(stderr, "[%d] %s\n", (int)errors.entries[i].severity,
                            errors.entries[i].message ? errors.entries[i].message : "(null)");
                }
            }

            tg3_model_free(&model);
            tg3_error_stack_free(&errors);
        }
    private:
        const std::vector<Model> m_models {};
        std::vector<ModelInstance> m_modelInstances {};
};

#endif // !SCENE_HPP
