//
// Created by Debreky on 25/07/2026.
//

#include "Components.h"
#include "glm/glm.hpp"
#include <glm/gtc/matrix_transform.hpp>

namespace Osiris {
    glm::mat4 TransformComponent::GetModelMatrix() const {
        glm::mat4 model = glm::mat4(1.0f);

        model = glm::translate(model, position);
        model = glm::rotate(model, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f)); // X axis
        model = glm::rotate(model, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f)); // Y axis
        model = glm::rotate(model, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f)); // Z axis
        model = glm::scale(model, scale);

        return model;
    }

    glm::vec3 TransformComponent::GetForward() const {
        glm::mat4 rot = glm::mat4(1.0f);
        rot = glm::rotate(rot, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
        rot = glm::rotate(rot, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
        rot = glm::rotate(rot, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
        return glm::normalize(glm::vec3(rot * glm::vec4(0.0f, -1.0f, 0.0f, 0.0f)));
    }

}
