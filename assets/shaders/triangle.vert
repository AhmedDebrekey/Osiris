#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inTangent;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    mat4 lightSpaceMatrices[3];
    vec4 cascadeSplits;
    vec4 lightDirection;
    vec4 cameraPosition;
} camera;

// Must stay in sync with MAX_SPOT_LIGHTS / MAX_SPOT_SHADOW_CASTERS in Light.h.
struct SpotLightGPU {
    vec4 position;
    vec4 direction;
    vec4 color;
    vec4 params;
};

layout(set = 0, binding = 5) uniform SpotLightUBO {
    mat4 shadowMatrices[3];
    SpotLightGPU lights[8];
    ivec4 counts;
} spotLights;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 emissive;
} push;

layout(location = 0) out vec3 outNormal;
layout(location = 1) out vec2 outTexCoord;
layout(location = 2) out vec3 outWorldPos;
layout(location = 3) out vec4 outShadowCoord0;
layout(location = 4) out vec4 outShadowCoord1;
layout(location = 5) out vec4 outShadowCoord2;
layout(location = 6) out vec3 outTangent;
layout(location = 7) out vec3 outBitangent;
layout(location = 8) out vec4 outShadowCoordSpot[3];

void main() {
    vec4 worldPos = push.model * vec4(inPosition, 1.0);
    gl_Position   = camera.projection * camera.view * worldPos;

    mat3 normalMatrix = mat3(push.model);

    vec3 N = normalize(normalMatrix * inNormal);
    vec3 T = normalize(normalMatrix * inTangent.xyz);
    vec3 B = cross(N, T) * inTangent.w;

    outNormal       = N;
    outTangent      = T;
    outBitangent    = B;
    outTexCoord     = inTexCoord;
    outWorldPos     = worldPos.xyz;
    outShadowCoord0 = camera.lightSpaceMatrices[0] * worldPos;
    outShadowCoord1 = camera.lightSpaceMatrices[1] * worldPos;
    outShadowCoord2 = camera.lightSpaceMatrices[2] * worldPos;
    for (int i = 0; i < 3; i++) {
        outShadowCoordSpot[i] = spotLights.shadowMatrices[i] * worldPos;
    }
}
