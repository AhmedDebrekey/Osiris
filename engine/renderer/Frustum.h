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

} // Osiris

#endif //OSIRIS_FRUSTUM_H