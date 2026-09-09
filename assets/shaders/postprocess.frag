#version 450

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D sceneColor;
layout(set = 0, binding = 1) uniform sampler2D bloomColor;

// The first seven floats mirror PostProcessSettings in engine/renderer/Light.h.
// The complete block mirrors PostProcessPushConstants in VulkanRHI.cpp.
layout(push_constant) uniform PostProcessPushConstants {
    float vignetteIntensity;
    float vignetteInnerRadius;
    float vignetteOuterRadius;
    float chromaticAberrationIntensity;
    float filmGrainIntensity;
    float bloomIntensity;
    float bloomRadius;
    float time;
    uint effectsEnabled;
    uint bloomAvailable;
} pc;

const float MAX_ABERRATION_OFFSET = 0.02;

float Grain(vec2 uv, float time) {
    vec2 seed = uv * 3000.0 + time;
    return fract(sin(dot(seed, vec2(12.9898, 78.233))) * 43758.5453123);
}

vec3 ToneMap(vec3 color) {
    color = max(color, vec3(0.0));
    return clamp((color * (2.51 * color + 0.03))
               / (color * (2.43 * color + 0.59) + 0.14), 0.0, 1.0);
}

void main() {
    vec2 dir = inUV - vec2(0.5);
    vec3 color = texture(sceneColor, inUV).rgb;
    if (pc.effectsEnabled != 0u) {
        vec2 aberrationOffset = dir * pc.chromaticAberrationIntensity * MAX_ABERRATION_OFFSET;
        color.r = texture(sceneColor, inUV - aberrationOffset).r;
        color.b = texture(sceneColor, inUV + aberrationOffset).b;
        if (pc.bloomAvailable != 0u && pc.bloomIntensity > 0.0) {
            color += texture(bloomColor, inUV).rgb * pc.bloomIntensity;
        }
    }

    // Even the plain Edit viewport needs HDR resolve. ImGui/UI is drawn after this pass,
    // and the sRGB destination encodes on write, so no manual gamma correction belongs here.
    color = ToneMap(color);

    if (pc.effectsEnabled != 0u) {
        float outerRadius = max(pc.vignetteOuterRadius, pc.vignetteInnerRadius + 0.001);
        float vignette = 1.0 - smoothstep(pc.vignetteInnerRadius, outerRadius, length(dir));
        color *= mix(1.0, vignette, pc.vignetteIntensity);
        color += (Grain(inUV, pc.time) - 0.5) * 2.0 * pc.filmGrainIntensity * 0.15;
    }
    outColor = vec4(color, 1.0);
}
