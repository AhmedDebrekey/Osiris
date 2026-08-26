#version 450

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D sceneColor;

// Mirrors Osiris::PostProcessSettings (engine/renderer/Light.h) field for field.
layout(set = 0, binding = 1) uniform PostProcessUBO {
    float vignetteIntensity;
    float vignetteInnerRadius;
    float vignetteOuterRadius;
    float chromaticAberrationIntensity;
    float filmGrainIntensity;
} settings;

// Wall-clock seconds, so grain animates frame to frame instead of looking like a static dirty
// lens, see VulkanRHI::DrawPostProcessFullscreen for where this comes from.
layout(push_constant) uniform PostProcessPushConstants {
    float time;
} pc;

// UV-space offset at chromaticAberrationIntensity = 1.0, one screen-space texel at typical
// resolutions is roughly 0.001 in UV terms, so this caps out at a clearly visible but not
// absurd fringe at the screen edges.
const float MAX_ABERRATION_OFFSET = 0.02;

// Cheap hash, not real noise, good enough for grain. Scaling the UV up first gives it enough
// spatial frequency to read as per-pixel texture instead of a smooth gradient, since this
// shader has no actual screen resolution to hash against.
float Grain(vec2 uv, float time) {
    vec2 seed = uv * 3000.0 + time;
    return fract(sin(dot(seed, vec2(12.9898, 78.233))) * 43758.5453123);
}

void main() {
    // dir grows linearly with distance from center already, so scaling it directly (rather than
    // normalizing then re-multiplying by distance) gives the same radial falloff and sidesteps
    // normalize()'s 0/0 NaN right at the exact center pixel.
    vec2 dir = inUV - vec2(0.5);
    vec2 aberrationOffset = dir * settings.chromaticAberrationIntensity * MAX_ABERRATION_OFFSET;

    vec3 color;
    color.r = texture(sceneColor, inUV - aberrationOffset).r;
    color.g = texture(sceneColor, inUV).g;
    color.b = texture(sceneColor, inUV + aberrationOffset).b;

    // smoothstep's result is undefined if edge0 >= edge1, reachable simply by dragging the
    // Inner/Outer Radius sliders past each other (or an out-of-order Lua assignment), so clamp
    // the gap here rather than relying on callers to keep the two ordered.
    float outerRadius = max(settings.vignetteOuterRadius, settings.vignetteInnerRadius + 0.001);
    float dist = length(dir);
    float vignette = 1.0 - smoothstep(settings.vignetteInnerRadius, outerRadius, dist);
    vignette = mix(1.0, vignette, settings.vignetteIntensity);
    color *= vignette;

    // Applied last, on the final graded color, same convention as a physical film/sensor
    // artifact rather than something the vignette itself should darken.
    float grain = (Grain(inUV, pc.time) - 0.5) * 2.0; // -1..1
    color += grain * settings.filmGrainIntensity * 0.15;

    outColor = vec4(color, 1.0);
}
