#version 450

layout(set = 0, binding = 6) uniform samplerCube environmentMap;

layout(push_constant) uniform PushConstants {
    float exposure;
} push;

layout(location = 0) in vec3 inDir;
layout(location = 0) out vec4 outColor;

void main() {
    vec3 color = texture(environmentMap, normalize(inDir)).rgb * push.exposure;

    // Tone mapping (ACES filmic approximation) — matches triangle.frag. No
    // manual gamma correction: the sRGB swapchain format encodes that on
    // write (see triangle.frag's comment on the same line for why).
    color = (color * (2.51 * color + 0.03)) / (color * (2.43 * color + 0.59) + 0.14);
    color = clamp(color, 0.0, 1.0);

    outColor = vec4(color, 1.0);
}
