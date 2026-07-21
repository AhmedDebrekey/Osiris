#define SDL_MAIN_HANDLED

#include "../../engine/core/Engine.h"

#include "renderer/MeshType.h"
#include "renderer/Camera.h"

#include <iostream>



int main() {
    std::cout << "Osiris Engine" << std::endl;
    Osiris::Engine engine;

    constexpr Osiris::Vertex vertices[] = {
        { .Position = {0.0f, -0.5f, 0.0f}, .Normal = {0.0f, 0.0f, 1.0f}, .TexCoord = {0.5f, 0.0f} },
        { .Position = {0.5f,  0.5f, 0.0f}, .Normal = {0.0f, 0.0f, 1.0f}, .TexCoord = {1.0f, 1.0f} },
        { .Position = {-0.5f, 0.5f, 0.0f}, .Normal = {0.0f, 0.0f, 1.0f}, .TexCoord = {0.0f, 1.0f} },
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

    const Osiris::Mesh mesh = {
        .vertexBuffer = vertexBuffer,
        .indexBuffer = indexBuffer,
        .vertexCount = sizeof(vertices) / sizeof(Osiris::Vertex),
        .indexCount = sizeof(indices) / sizeof(uint32_t),
    };

    engine.GetRHI()->SetMeshData(mesh);

    Osiris::Camera camera(
        glm::vec3(0.0f, 0.0f, 2.0f),  // position — 2 units back
        glm::vec3(0.0f, 0.0f, -1.0f)  // looking forward into the scene
    );
    uint64_t lastTime = SDL_GetTicks64();
    float deltaTime = 0.0f;
    while (engine.IsRunning()) {
        uint64_t currentTime = SDL_GetTicks64();
        deltaTime = (currentTime - lastTime) / 1000.0f; // convert ms to seconds
        lastTime = currentTime;
        engine.BeginFrame();
        camera.Update(*engine.GetInput(), deltaTime);
        engine.GetRHI()->UpdateCamera(camera.GetViewMatrix(), camera.GetProjectionMatrix());
        engine.EndFrame();
    }
    engine.Shutdown();
    return 0;
}
