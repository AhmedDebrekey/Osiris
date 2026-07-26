//
// Created by Debreky on 26/07/2026.
//

#include "Frustum.h"

namespace Osiris {

Frustum Frustum::FromViewProjection(const glm::mat4& vp) {
    Frustum frustum;

    glm::vec4 col0 = glm::vec4(vp[0][0], vp[1][0], vp[2][0], vp[3][0]);
    glm::vec4 col1 = glm::vec4(vp[0][1], vp[1][1], vp[2][1], vp[3][1]);
    glm::vec4 col2 = glm::vec4(vp[0][2], vp[1][2], vp[2][2], vp[3][2]);
    glm::vec4 col3 = glm::vec4(vp[0][3], vp[1][3], vp[2][3], vp[3][3]);

    frustum.planes[0] = col3 + col0; // left
    frustum.planes[1] = col3 - col0; // right
    frustum.planes[2] = col3 + col1; // bottom
    frustum.planes[3] = col3 - col1; // top
    frustum.planes[4] = col3 + col2; // near
    frustum.planes[5] = col3 - col2; // far

    // Normalize planes
    for (auto& plane : frustum.planes) {
        float length = glm::length(glm::vec3(plane));
        plane /= length;
    }

    return frustum;
}

bool Frustum::IsVisible(const AABB& bounds, const glm::mat4& model) const {
    // Transform AABB corners to world space and test against each plane
    glm::vec3 corners[8] = {
        glm::vec3(model * glm::vec4(bounds.min.x, bounds.min.y, bounds.min.z, 1.0f)),
        glm::vec3(model * glm::vec4(bounds.max.x, bounds.min.y, bounds.min.z, 1.0f)),
        glm::vec3(model * glm::vec4(bounds.min.x, bounds.max.y, bounds.min.z, 1.0f)),
        glm::vec3(model * glm::vec4(bounds.max.x, bounds.max.y, bounds.min.z, 1.0f)),
        glm::vec3(model * glm::vec4(bounds.min.x, bounds.min.y, bounds.max.z, 1.0f)),
        glm::vec3(model * glm::vec4(bounds.max.x, bounds.min.y, bounds.max.z, 1.0f)),
        glm::vec3(model * glm::vec4(bounds.min.x, bounds.max.y, bounds.max.z, 1.0f)),
        glm::vec3(model * glm::vec4(bounds.max.x, bounds.max.y, bounds.max.z, 1.0f)),
    };

    for (const auto& plane : planes) {
        bool allOutside = true;
        for (const auto& corner : corners) {
            float dist = glm::dot(glm::vec3(plane), corner) + plane.w;
            if (dist >= 0.0f) {
                allOutside = false;
                break;
            }
        }
        if (allOutside) return false;
    }
    return true;
}

} // Osiris