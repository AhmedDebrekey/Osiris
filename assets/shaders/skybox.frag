#version 450

layout(set = 0, binding = 6) uniform samplerCube environmentMap;

layout(push_constant) uniform PushConstants {
    float exposure;
} push;

layout(location = 0) in vec3 inDir;
layout(location = 0) out vec4 outColor;

void main() {
    vec3 color = texture(environmentMap, normalize(inDir)).rgb * push.exposure;

    // Match the forward pass's HDR target. Bloom and tone mapping run in the final composite.
    color = clamp(color, vec3(0.0), vec3(64000.0));

    outColor = vec4(color, 1.0);
}
