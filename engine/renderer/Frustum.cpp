//
// Created by Debreky on 26/07/2026.
//

#include "Frustum.h"

#include <algorithm>
#include <array>
#include <limits>

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
    const std::array<glm::vec3, 8> corners = bounds.GetWorldCorners(model);

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

bool RayIntersectsAABB(const glm::vec3& rayOrigin, const glm::vec3& rayDir,
    const AABB& bounds, const glm::mat4& model, float& outDistance,
    bool useExitDistanceWhenInside) {
    const glm::mat4 invModel = glm::inverse(model);
    const glm::vec3 localOrigin = glm::vec3(invModel * glm::vec4(rayOrigin, 1.0f));
    const glm::vec3 localDir = glm::vec3(invModel * glm::vec4(rayDir, 0.0f));

    float tMin = -std::numeric_limits<float>::max();
    float tMax = std::numeric_limits<float>::max();

    for (int axis = 0; axis < 3; axis++) {
        if (glm::abs(localDir[axis]) < 1e-8f) {
            if (localOrigin[axis] < bounds.min[axis] || localOrigin[axis] > bounds.max[axis]) return false;
            continue;
        }

        const float invDir = 1.0f / localDir[axis];
        float t1 = (bounds.min[axis] - localOrigin[axis]) * invDir;
        float t2 = (bounds.max[axis] - localOrigin[axis]) * invDir;
        if (t1 > t2) std::swap(t1, t2);

        tMin = glm::max(tMin, t1);
        tMax = glm::min(tMax, t2);
        if (tMin > tMax) return false;
    }

    if (tMax < 0.0f) return false; // box is entirely behind the ray origin

    // tMin < 0 here means the unclamped near intersection is behind the origin, i.e. the ray
    // origin is genuinely inside the box, not merely touching its surface (which gives tMin
    // exactly 0 without ever going negative): that distinction is what tMin == 0.0f used to get
    // wrong, reporting the exit distance for an ordinary surface hit too.
    const bool startsInside = tMin < 0.0f;
    outDistance = (useExitDistanceWhenInside && startsInside) ? tMax : glm::max(tMin, 0.0f);
    return true;
}

} // Osiris
