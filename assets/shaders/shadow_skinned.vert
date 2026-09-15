#version 450
#extension GL_GOOGLE_include_directive : require
#include "skinning.glsl"

layout(location = 0) in vec3 inPosition;
layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 lightSpaceMatrix;
} push;

void main() {
    gl_Position = push.lightSpaceMatrix * push.model * SkinMatrix() * vec4(inPosition, 1.0);
}
