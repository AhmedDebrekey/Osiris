//
// Created by Debreky on 25/07/2026.
//

#include "Components.h"
#include "glm/glm.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>

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

    glm::vec3 TransformComponent::GetForwardXZ() const {
        const float yaw = glm::radians(rotation.y);
        return glm::vec3(-sin(yaw), 0.0f, -cos(yaw));
    }

    glm::vec3 TransformComponent::GetRightXZ() const {
        const float yaw = glm::radians(rotation.y);
        return glm::vec3(cos(yaw), 0.0f, -sin(yaw));
    }

    glm::vec3 TransformComponent::ExtractRotation(const glm::mat4& matrix, const glm::vec3& reference) {
        glm::mat3 rotationMatrix(matrix);
        rotationMatrix[0] = glm::normalize(rotationMatrix[0]);
        rotationMatrix[1] = glm::normalize(rotationMatrix[1]);
        rotationMatrix[2] = glm::normalize(rotationMatrix[2]);

        const float ry = std::asin(glm::clamp(rotationMatrix[2][0], -1.0f, 1.0f));
        const float rx = std::atan2(-rotationMatrix[2][1], rotationMatrix[2][2]);
        const float rz = std::atan2(-rotationMatrix[1][0], rotationMatrix[0][0]);

        auto unwrapDegrees = [](float angle, float previous) {
            return previous + std::remainder(angle - previous, 360.0f);
        };
        glm::vec3 primary = glm::degrees(glm::vec3(rx, ry, rz));
        glm::vec3 alternate = glm::degrees(glm::vec3(
            rx + glm::pi<float>(), glm::pi<float>() - ry, rz + glm::pi<float>()));
        for (int axis = 0; axis < 3; ++axis) {
            primary[axis] = unwrapDegrees(primary[axis], reference[axis]);
            alternate[axis] = unwrapDegrees(alternate[axis], reference[axis]);
        }

        const glm::vec3 primaryDelta = primary - reference;
        const glm::vec3 alternateDelta = alternate - reference;
        return glm::dot(primaryDelta, primaryDelta) <= glm::dot(alternateDelta, alternateDelta)
            ? primary : alternate;
    }

    void TransformComponent::SetFromMatrix(const glm::mat4& matrix) {
        position = glm::vec3(matrix[3]);
        rotation = ExtractRotation(matrix, rotation);
        scale = {
            glm::length(glm::vec3(matrix[0])),
            glm::length(glm::vec3(matrix[1])),
            glm::length(glm::vec3(matrix[2])),
        };
    }
}
