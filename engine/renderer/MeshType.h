//
// Created by Debreky on 05/06/2026.
//

#ifndef OSIRIS_MESHTYPE_H
#define OSIRIS_MESHTYPE_H
#include "glm/glm.hpp"
#include "rhi/RHITypes.h"
#include <array>
namespace Osiris
{
    struct Vertex {
        glm::vec3 Position;
        glm::vec3 Normal;
        glm::vec2 TexCoord;
        glm::vec4 Tangent;
    };

    struct AABB {
        glm::vec3 min = glm::vec3( std::numeric_limits<float>::max());
        glm::vec3 max = glm::vec3(-std::numeric_limits<float>::max());

        // The single shared place to turn a box into its 8 world-space corners: callers that
        // need a center/halfExtents box (e.g. ColliderComponent) convert to min/max first
        // (center - halfExtents, center + halfExtents) rather than re-deriving corners by hand.
        std::array<glm::vec3, 8> GetWorldCorners(const glm::mat4& model) const {
            return {
                glm::vec3(model * glm::vec4(min.x, min.y, min.z, 1.0f)),
                glm::vec3(model * glm::vec4(max.x, min.y, min.z, 1.0f)),
                glm::vec3(model * glm::vec4(min.x, max.y, min.z, 1.0f)),
                glm::vec3(model * glm::vec4(max.x, max.y, min.z, 1.0f)),
                glm::vec3(model * glm::vec4(min.x, min.y, max.z, 1.0f)),
                glm::vec3(model * glm::vec4(max.x, min.y, max.z, 1.0f)),
                glm::vec3(model * glm::vec4(min.x, max.y, max.z, 1.0f)),
                glm::vec3(model * glm::vec4(max.x, max.y, max.z, 1.0f)),
            };
        }
    };

    struct Mesh {
        BufferHandle vertexBuffer   = BufferHandle();
        BufferHandle indexBuffer    = BufferHandle();
        uint32_t     vertexCount    = 0;
        uint32_t     indexCount     = 0;
        AABB bounds;
    };

    struct MeshPrimitive {
        Mesh           mesh;
        MaterialHandle material;
    };

}

#endif //OSIRIS_MESHTYPE_H