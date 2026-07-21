#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

layout(binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
} camera;

layout(push_constant) uniform PushConstants {
    mat4 model;
} push;

layout(location = 0) out vec3 outNormal;
layout(location = 1) out vec2 outTexCoord;

void main() {
    gl_Position = camera.projection * camera.view * push.model * vec4(inPosition, 1.0);
    outNormal   = mat3(transpose(inverse(push.model))) * inNormal;
    outTexCoord = inTexCoord;
}