//
// Created by Debreky on 07/08/2026.
//

#ifndef OSIRIS_LIGHT_H
#define OSIRIS_LIGHT_H

#include <glm/glm.hpp>

namespace Osiris {

    struct DirectionalLight {
        glm::vec3 direction = glm::normalize(glm::vec3(-0.5f, -1.0f, -0.3f));
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

    struct SpotLight {
        glm::vec3 position          = glm::vec3(0.0f, 2.0f, 0.0f);
        glm::vec3 direction         = glm::vec3(0.0f, -1.0f, 0.0f);
        glm::vec3 color             = glm::vec3(1.0f, 1.0f, 1.0f);
        float     intensity         = 10.0f;
        float     innerConeDegrees  = 15.0f;
        float     outerConeDegrees  = 20.0f;
        float     range             = 10.0f;
    };

}

#endif //OSIRIS_LIGHT_H