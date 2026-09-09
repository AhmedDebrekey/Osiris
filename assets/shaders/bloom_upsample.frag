#version 450

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;
layout(set = 0, binding = 0) uniform sampler2D lowerMip;
layout(set = 0, binding = 1) uniform sampler2D detailMip;

// Mirrors BloomPushConstants in engine/rhi_vulkan/VulkanRHI.cpp.
layout(push_constant) uniform BloomPushConstants {
    float radius;
    uint firstDownsample;
    float detailWeight;
} pc;

void main() {
    vec2 stepUV = pc.radius / vec2(textureSize(lowerMip, 0));
    vec3 filtered = texture(lowerMip, inUV).rgb * 4.0;
    filtered += texture(lowerMip, inUV + stepUV * vec2(-1.0,  0.0)).rgb * 2.0;
    filtered += texture(lowerMip, inUV + stepUV * vec2( 1.0,  0.0)).rgb * 2.0;
    filtered += texture(lowerMip, inUV + stepUV * vec2( 0.0, -1.0)).rgb * 2.0;
    filtered += texture(lowerMip, inUV + stepUV * vec2( 0.0,  1.0)).rgb * 2.0;
    filtered += texture(lowerMip, inUV + stepUV * vec2(-1.0, -1.0)).rgb;
    filtered += texture(lowerMip, inUV + stepUV * vec2( 1.0, -1.0)).rgb;
    filtered += texture(lowerMip, inUV + stepUV * vec2(-1.0,  1.0)).rgb;
    filtered += texture(lowerMip, inUV + stepUV * vec2( 1.0,  1.0)).rgb;
    filtered *= 1.0 / 16.0;

    // The progressive sum (slide 161), normalized as we go to keep the packed HDR target
    // in range and the intensity independent of how many levels fit a small viewport.
    vec3 detail = texture(detailMip, inUV).rgb;
    outColor = vec4(mix(filtered, detail, pc.detailWeight), 1.0);
}
