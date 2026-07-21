#version 450

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec2 inTexCoord;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));
    vec3 normal   = normalize(inNormal);
    float diff    = max(dot(normal, lightDir), 0.0);
    vec3 color    = vec3(0.8, 0.2, 0.2) * diff + vec3(0.1);
    outColor      = vec4(color, 1.0);
}