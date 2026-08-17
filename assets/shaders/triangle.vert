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
    mat4 spotLightSpaceMatrix;
    vec4 spotPosition;
    vec4 spotDirection;
    vec4 spotParams;
} camera;

layout(push_constant) uniform PushConstants {
    mat4 model;
} push;

layout(location = 0) out vec3 outNormal;
layout(location = 1) out vec2 outTexCoord;
layout(location = 2) out vec3 outWorldPos;
layout(location = 3) out vec4 outShadowCoord0;
layout(location = 4) out vec4 outShadowCoord1;
layout(location = 5) out vec4 outShadowCoord2;
layout(location = 6) out vec3 outTangent;
layout(location = 7) out vec3 outBitangent;
layout(location = 8) out vec4 outShadowCoordSpot;

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
    outShadowCoordSpot = camera.spotLightSpaceMatrix * worldPos;
}