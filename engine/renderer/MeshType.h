//
// Created by Debreky on 05/06/2026.
//

#ifndef OSIRIS_MESHTYPE_H
#define OSIRIS_MESHTYPE_H
#include "glm/glm.hpp"
#include "rhi/RHITypes.h"
namespace Osiris
{
    struct Vertex {
        glm::vec3 Position;
        glm::vec3 Normal;
        glm::vec2 TexCoord;
    };

    struct Mesh {
        BufferHandle vertexBuffer   = BufferHandle();
        BufferHandle indexBuffer    = BufferHandle();
        uint32_t     vertexCount    = 0;
        uint32_t     indexCount     = 0;
    };
}

#endif //OSIRIS_MESHTYPE_H