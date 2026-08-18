#version 450

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    mat4 lightSpaceMatrices[3];
    vec4 cascadeSplits;
    vec4 lightDirection;
    vec4 cameraPosition;
} camera;

layout(location = 0) out vec3 outDir;

// Hardcoded unit cube — no vertex buffer needed (see PipelineDesc::vertexInput
// = false). Each position doubles as the cubemap sample direction.
const vec3 CUBE_POSITIONS[36] = vec3[](
    vec3(-1,-1,-1), vec3(-1,-1, 1), vec3(-1, 1, 1),
    vec3(-1, 1, 1), vec3(-1, 1,-1), vec3(-1,-1,-1),

    vec3( 1,-1,-1), vec3( 1, 1,-1), vec3( 1, 1, 1),
    vec3( 1, 1, 1), vec3( 1,-1, 1), vec3( 1,-1,-1),

    vec3(-1,-1,-1), vec3( 1,-1,-1), vec3( 1,-1, 1),
    vec3( 1,-1, 1), vec3(-1,-1, 1), vec3(-1,-1,-1),

    vec3(-1, 1,-1), vec3(-1, 1, 1), vec3( 1, 1, 1),
    vec3( 1, 1, 1), vec3( 1, 1,-1), vec3(-1, 1,-1),

    vec3(-1,-1,-1), vec3(-1, 1,-1), vec3( 1, 1,-1),
    vec3( 1, 1,-1), vec3( 1,-1,-1), vec3(-1,-1,-1),

    vec3(-1,-1, 1), vec3( 1,-1, 1), vec3( 1, 1, 1),
    vec3( 1, 1, 1), vec3(-1, 1, 1), vec3(-1,-1, 1)
);

void main() {
    vec3 pos = CUBE_POSITIONS[gl_VertexIndex];
    outDir = pos;

    // Drop the view matrix's translation — the skybox always surrounds the
    // camera, only its rotation matters.
    mat4 viewRotationOnly = mat4(mat3(camera.view));
    vec4 clipPos = camera.projection * viewRotationOnly * vec4(pos, 1.0);

    // Force the depth to the far plane (z/w = 1.0) so the skybox always
    // renders behind real geometry regardless of the cube's actual size.
    gl_Position = clipPos.xyww;
}
