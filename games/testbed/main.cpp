#include "../../engine/core/Engine.h"

#include "renderer/MeshType.h"
#include "renderer/Camera.h"

#include <iostream>

int main() {
    std::cout << "Osiris Engine" << std::endl;
    Osiris::Engine engine;

    constexpr Vertex vertices[] = {
        { 0.0f, -0.5f, 0.0f},
        { 0.5f,  0.5f, 0.0f},
        {-0.5f,  0.5f, 0.0f},
    };
    constexpr BufferDesc vertexBufferDesc = {
        .size       = sizeof(vertices),
        .usage      = BufferUsage::Vertex,
        .cpuVisible = false,
    };

    constexpr uint32_t indices[] = { 0, 1, 2 };

    constexpr BufferDesc indexBufferDesc = {
        .size       = sizeof(indices),
        .usage      = BufferUsage::Index,
        .cpuVisible = false,
    };

    if (!engine.Initialize()) {
        OSIRIS_ERROR("Failed to initialize engine!");
        return -1;
    }



    const BufferHandle vertexBuffer = engine.GetRHI()->CreateBuffer(vertexBufferDesc);
    engine.GetRHI()->UploadBufferData(vertexBuffer, vertices, sizeof(vertices));

    const BufferHandle indexBuffer = engine.GetRHI()->CreateBuffer(indexBufferDesc);
    engine.GetRHI()->UploadBufferData(indexBuffer, indices, sizeof(indices));

    const Mesh mesh = {
        .vertexBuffer = vertexBuffer,
        .indexBuffer = indexBuffer,
        .vertexCount = sizeof(vertices) / sizeof(Vertex),
        .indexCount = sizeof(indices) / sizeof(uint32_t),
    };

    engine.GetRHI()->SetMeshData(mesh);

    Camera camera(
        glm::vec3(0.0f, 0.0f, 2.0f),  // position — 2 units back
        glm::vec3(0.0f, 0.0f, -1.0f)  // looking forward into the scene
    );

    while (engine.IsRunning()) {
        engine.BeginFrame();
        engine.GetRHI()->UpdateCamera(camera.GetViewMatrix(), camera.GetProjectionMatrix());
        engine.EndFrame();
    }
    engine.Shutdown();
    return 0;
}
