//
// Created by Debreky on 05/06/2026.
//

#ifndef OSIRIS_MESHTYPE_H
#define OSIRIS_MESHTYPE_H
#include "glm/glm.hpp"
#include "rhi/RHITypes.h"
#include <array>
#include <memory>
#include <vector>
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

    struct SkinVertex {
        // Matches binding 1 in PipelineManager and locations 4/5 in skinning.glsl.
        glm::uvec4 joints{0};
        glm::vec4 weights{0.0f};
    };
    static_assert(sizeof(SkinVertex) == 32 && offsetof(SkinVertex, weights) == 16);

    struct Mesh {
        BufferHandle vertexBuffer   = BufferHandle();
        BufferHandle indexBuffer    = BufferHandle();
        uint32_t     vertexCount    = 0;
        uint32_t     indexCount     = 0;
        AABB bounds;
        BufferHandle skinVertexBuffer;
        std::shared_ptr<const std::vector<AABB>> jointBounds;
    };

    struct MeshPrimitive {
        Mesh           mesh;
        MaterialHandle material;
    };

}

#endif //OSIRIS_MESHTYPE_H
