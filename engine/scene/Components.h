//
// Created by Debreky on 25/07/2026.
//

#ifndef OSIRIS_COMPONENTS_H
#define OSIRIS_COMPONENTS_H

#include <glm/glm.hpp>

#include "renderer/MeshType.h"
#include "rhi/RHITypes.h"

namespace Osiris {
    struct TransformComponent {
        glm::vec3 position = {0,0,0};
        glm::vec3 rotation = {0,0,0}; // euler angles
        glm::vec3 scale    = {1,1,1};
        glm::mat4 GetModelMatrix() const;
    };

    struct MeshComponent {
        Mesh mesh;
    };

    struct MaterialComponent {
        MaterialHandle material;
        // more later for PBR
    };

    struct TagComponent {
        std::string name;
    };

    struct SpotLightComponent {
        glm::vec3 color     = glm::vec3(1.0f);
        float intensity     = 1.0f;
        float innerCone     = 12.5f;
        float outerCone     = 17.5f;
        float range         = 10.f;
        bool enabled        = true;
        bool castsShadow    = true;
    };
}

#endif //OSIRIS_COMPONENTS_H