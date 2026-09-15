layout(location = 4) in uvec4 inJoints;
layout(location = 5) in vec4 inWeights;
layout(set = 2, binding = 0, std430) readonly buffer SkinPalette {
    mat4 joints[];
} skin;

mat4 SkinMatrix() {
    return inWeights.x * skin.joints[inJoints.x]
         + inWeights.y * skin.joints[inJoints.y]
         + inWeights.z * skin.joints[inJoints.z]
         + inWeights.w * skin.joints[inJoints.w];
}
