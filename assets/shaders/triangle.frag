#version 450

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec2 inTexCoord;

layout(set = 1, binding = 0) uniform sampler2D texSampler;
layout(location = 0) out vec4 outColor;

void main() {
    vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));
    vec3 normal   = normalize(inNormal);
    float diff    = max(dot(normal, lightDir), 0.0) * 1.5;

    vec4 texColor = texture(texSampler, inTexCoord);

    // Gamma correct the texture (it's stored in sRGB space)
    vec3 linearColor = pow(texColor.rgb, vec3(2.2));

    vec3 lit = linearColor * diff + linearColor * 0.15;

    // Gamma correct output
    vec3 gammaCorrect = pow(lit, vec3(1.0/2.2));
    outColor = vec4(gammaCorrect, texColor.a);
}