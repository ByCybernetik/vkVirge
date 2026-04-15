#pragma once

#include "vk_utils_core.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace vk_utils {

    // Структура вершины для куба (с GLM)
    struct CubeVertex {
        glm::vec3 pos;
        glm::vec3 color;
        glm::vec2 texCoord;
    };

    // Uniform buffer object (как в примере)
    struct UniformBufferObject {
        glm::mat4 model;
        glm::mat4 view;
        glm::mat4 proj;
    };

} // namespace vk_utils
