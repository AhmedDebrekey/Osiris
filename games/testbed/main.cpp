#include "../../engine/core/Engine.h"
#include <iostream>

#include "renderer/MeshType.h"

int main() {
    std::cout << "Osiris Engine" << std::endl;
    Osiris::Engine engine;
    Vertex vertices[] = {
        { 0.0f, -0.5f, 0.0f},
        { 0.5f,  0.5f, 0.0f},
        {-0.5f,  0.5f, 0.0f},
    };
    BufferDesc vertexBufferDesc = {
        .size       = sizeof(vertices),
        .usage      = BufferUsage::Vertex,
        .cpuVisible = false,
    };

    engine.Initialize();
    const BufferHandle vertexBuffer = engine.GetRHI()->CreateBuffer(vertexBufferDesc);
    engine.GetRHI()->UploadBufferData(vertexBuffer, vertices, sizeof(vertices));
    engine.GetRHI()->SetVertexBuffer(vertexBuffer);
    engine.Run();
    engine.Shutdown();
    return 0;
}
