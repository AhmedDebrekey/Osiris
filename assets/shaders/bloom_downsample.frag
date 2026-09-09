#version 450

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;
layout(set = 0, binding = 0) uniform sampler2D sourceColor;

// Mirrors BloomPushConstants in engine/rhi_vulkan/VulkanRHI.cpp.
layout(push_constant) uniform BloomPushConstants {
    float radius;
    uint firstDownsample;
    float detailWeight;
} pc;

float KarisWeight(vec3 color) {
    return 1.0 / (1.0 + dot(color, vec3(0.2126, 0.7152, 0.0722)));
}

vec3 AverageGroup(vec3 a, vec3 b, vec3 c, vec3 d) {
    if (pc.firstDownsample == 0u) {
        return (a + b + c + d) * 0.25;
    }
    // Partial Karis average: normalize within each four-fetch group, not across all 13 taps.
    // This preserves constant HDR fields while reducing isolated bright subpixels (slides 167-168).
    vec4 weights = vec4(KarisWeight(a), KarisWeight(b), KarisWeight(c), KarisWeight(d));
    return (a * weights.x + b * weights.y + c * weights.z + d * weights.w)
         / dot(weights, vec4(1.0));
}

void main() {
    vec2 texel = 1.0 / vec2(textureSize(sourceColor, 0));
    vec3 a = texture(sourceColor, inUV + texel * vec2(-2.0,  2.0)).rgb;
    vec3 b = texture(sourceColor, inUV + texel * vec2( 0.0,  2.0)).rgb;
    vec3 c = texture(sourceColor, inUV + texel * vec2( 2.0,  2.0)).rgb;
    vec3 d = texture(sourceColor, inUV + texel * vec2(-2.0,  0.0)).rgb;
    vec3 e = texture(sourceColor, inUV).rgb;
    vec3 f = texture(sourceColor, inUV + texel * vec2( 2.0,  0.0)).rgb;
    vec3 g = texture(sourceColor, inUV + texel * vec2(-2.0, -2.0)).rgb;
    vec3 h = texture(sourceColor, inUV + texel * vec2( 0.0, -2.0)).rgb;
    vec3 i = texture(sourceColor, inUV + texel * vec2( 2.0, -2.0)).rgb;
    vec3 j = texture(sourceColor, inUV + texel * vec2(-1.0,  1.0)).rgb;
    vec3 k = texture(sourceColor, inUV + texel * vec2( 1.0,  1.0)).rgb;
    vec3 l = texture(sourceColor, inUV + texel * vec2(-1.0, -1.0)).rgb;
    vec3 m = texture(sourceColor, inUV + texel * vec2( 1.0, -1.0)).rgb;

    // Jimenez, Next Generation Post Processing in CoD: Advanced Warfare, slide 152.
    vec3 filtered = AverageGroup(j, k, l, m) * 0.5;
    filtered += AverageGroup(a, b, d, e) * 0.125;
    filtered += AverageGroup(b, c, e, f) * 0.125;
    filtered += AverageGroup(d, e, g, h) * 0.125;
    filtered += AverageGroup(e, f, h, i) * 0.125;
    outColor = vec4(max(filtered, vec3(0.0)), 1.0);
}
