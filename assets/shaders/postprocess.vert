#version 450

layout(location = 0) out vec2 outUV;

// Classic 3-vertex fullscreen triangle, no vertex buffer needed (see
// PipelineDesc::vertexInput = false), same "hardcoded geometry in the vertex
// shader" trick the skybox uses for its cube. The triangle overshoots the
// [-1,1] clip volume on two corners; that's fine, the rasterizer clips it.
void main() {
    outUV = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(outUV * 2.0 - 1.0, 0.0, 1.0);
}
