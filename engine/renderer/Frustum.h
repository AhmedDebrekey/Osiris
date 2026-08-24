//
// Created by Debreky on 26/07/2026.
//

#ifndef OSIRIS_FRUSTUM_H
#define OSIRIS_FRUSTUM_H
#include <glm/glm.hpp>
#include "renderer/MeshType.h"

namespace Osiris {

    struct Frustum {
        glm::vec4 planes[6]; // left, right, bottom, top, near, far

        static Frustum FromViewProjection(const glm::mat4& vp);
        bool IsVisible(const AABB& bounds, const glm::mat4& model) const;
    };

    // Slab test in the AABB's own local space (ray inverse-transformed by model), so rotation
    // doesn't need a separate OBB test. On a hit, outDistance is the ray-origin-relative distance
    // to the nearest intersection (clamped to 0 if the origin starts inside the box).
    bool RayIntersectsAABB(const glm::vec3& rayOrigin, const glm::vec3& rayDir,
        const AABB& bounds, const glm::mat4& model, float& outDistance);

} // Osiris

#endif //OSIRIS_FRUSTUM_H