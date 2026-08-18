//
// Created by Debreky on 07/08/2026.
//

#ifndef OSIRIS_LIGHT_H
#define OSIRIS_LIGHT_H

#include <cstdint>
#include <glm/glm.hpp>

namespace Osiris {

    struct DirectionalLight {
        glm::vec3 direction = glm::normalize(glm::vec3(-0.5f, 1.0f, -0.3f));
        glm::vec3 color     = glm::vec3(1.0f, 1.0f, 1.0f);
        float     intensity = 1.0f;
    };

    struct ShadowSettings {
        float nearClip           = 0.5f;
        float farClip            = 80.0f;
        float cascadeSplitLambda = 0.95f;
        float depthBiasConstant  = 1.25f;
        float depthBiasSlope     = 1.75f;
    };

    // Keep in sync with triangle.vert / triangle.frag.
    constexpr uint32_t MAX_SPOT_LIGHTS         = 8;
    constexpr uint32_t MAX_SPOT_SHADOW_CASTERS = 3;

    // Per-light data passed to IRHI::UpdateSpotLights each frame.
    struct SpotLightRenderData {
        glm::vec3 position;
        glm::vec3 direction;
        glm::vec3 color            = glm::vec3(1.0f);
        float     intensity        = 1.0f;
        float     innerConeDegrees = 12.5f;
        float     outerConeDegrees = 17.5f;
        float     range            = 10.0f;
        int       shadowIndex      = -1; // -1 = no shadow
    };

}

#endif //OSIRIS_LIGHT_H