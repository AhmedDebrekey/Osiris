#include "../../engine/core/Engine.h"
#include <iostream>

#include "renderer/MeshType.h"

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

    engine.Initialize();


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

    engine.Run();
    engine.Shutdown();
    return 0;
}
