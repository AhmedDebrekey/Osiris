#version 450

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec3 inWorldPos;
layout(location = 3) in vec4 inShadowCoord0;
layout(location = 4) in vec4 inShadowCoord1;
layout(location = 5) in vec4 inShadowCoord2;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    mat4 lightSpaceMatrices[3];
    vec4 cascadeSplits;
} camera;

layout(set = 0, binding = 1) uniform sampler2DShadow shadowMap0;
layout(set = 0, binding = 2) uniform sampler2DShadow shadowMap1;
layout(set = 0, binding = 3) uniform sampler2DShadow shadowMap2;

layout(set = 1, binding = 0) uniform sampler2D texSampler;

layout(location = 0) out vec4 outColor;

float SampleShadowPCF(sampler2DShadow shadowMap, vec4 shadowCoord) {
    vec3 proj = shadowCoord.xyz / shadowCoord.w;
    proj.xy   = proj.xy * 0.5 + 0.5;

    if (proj.z > 1.0 || proj.x < 0.0 || proj.x > 1.0 ||
    proj.y < 0.0 || proj.y > 1.0) {
        return 1.0;
    }

    float shadow   = 0.0;
    float bias     = 0.005;
    vec2 texelSize = 1.0 / vec2(textureSize(shadowMap, 0));

    for (int x = -1; x <= 1; x++) {
        for (int y = -1; y <= 1; y++) {
            shadow += texture(shadowMap,
                              vec3(proj.xy + vec2(x, y) * texelSize, proj.z - bias));
        }
    }
    return shadow / 9.0;
}

void main() {
    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.3));
    vec3 normal   = normalize(inNormal);
    float diff = max(dot(normal, -lightDir), 0.0);

    // Cascade selection based on view-space depth
    float depth = abs((camera.view * vec4(inWorldPos, 1.0)).z);

    float shadow;
    if (depth < abs(camera.cascadeSplits.x)) {
        shadow = SampleShadowPCF(shadowMap0, inShadowCoord0);
    } else if (depth < abs(camera.cascadeSplits.y)) {
        shadow = SampleShadowPCF(shadowMap1, inShadowCoord1);
    } else {
        shadow = SampleShadowPCF(shadowMap2, inShadowCoord2);
    }

    vec4 texColor    = texture(texSampler, inTexCoord);
    vec3 linearColor = pow(texColor.rgb, vec3(2.2));
    vec3 lit         = linearColor * (diff * shadow) + linearColor * 0.15;
    vec3 gamma       = pow(lit, vec3(1.0 / 2.2));
    outColor         = vec4(gamma, texColor.a);
}