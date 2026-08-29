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
        float nearClip           = 5.0f;
        float farClip            = 80.0f;
        float cascadeSplitLambda = 0.95f;
        float depthBiasConstant  = 1.25f;
        float depthBiasSlope     = 1.75f;
    };

    // Mirrored by hand in postprocess.frag's PostProcessUBO: plain floats only, so C++ and
    // GLSL std140 agree on layout with no manual padding needed (unlike vec3/vec4/mat4 mixes
    // elsewhere, e.g. CameraBufferFull).
    struct PostProcessSettings {
        float vignetteIntensity   = 1.0f; // 0 = no effect, 1 = full strength
        float vignetteInnerRadius = 0.35f; // distance from center where darkening starts
        float vignetteOuterRadius = 0.75f; // distance from center where it reaches full strength
        float chromaticAberrationIntensity = 0.0f; // 0 = off, 1 = full strength, off by default
        float filmGrainIntensity = 0.0f; // 0 = off, 1 = full strength, off by default
        float bloomIntensity = 0.0f; // 0 = off, 1 = full strength, off by default
        float bloomThreshold = 0.8f; // LDR brightness where the soft bright pass starts
        float bloomRadius = 4.0f; // blur radius in output pixels
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
