#define SDL_MAIN_HANDLED

#include "../../engine/core/Engine.h"

#include "renderer/MeshType.h"
#include "renderer/Camera.h"
#include "assets/MeshLoader.h"

#include <iostream>

#include <glm/gtc/matrix_transform.hpp>

#include "assets/TextureLoader.h"
#include "core/AssetManager.h"


int main() {
    std::cout << "Osiris Engine" << std::endl;
    Osiris::Engine engine;

    if (!engine.Initialize()) {
        OSIRIS_ERROR("Failed to initialize engine!");
        return -1;
    }

    Osiris::Mesh Box = Osiris::MeshLoader::LoadFromGLTF(Osiris::AssetManager::GetPath("models/BoxTexturedGLTF/BoxTextured.gltf"), engine.GetRHI());
    TextureHandle boxTexture = Osiris::TextureLoader::LoadFromFile(Osiris::AssetManager::GetPath("textures/box.png"), engine.GetRHI());
    engine.GetRHI()->BindTexture(boxTexture);
    OSIRIS_INFO("Texture handle id: {}", boxTexture.id);
    engine.GetRHI()->SetModelMatrix(glm::translate(glm::mat4(1.0f), glm::vec3(2.0f, 0.0f, 0.0f)));
    engine.GetRHI()->SetMeshData(Box);

    Osiris::Camera camera(
        glm::vec3(0.0f, 0.0f, 2.0f),  // position — 2 units back
        glm::vec3(0.0f, 0.0f, -1.0f)  // looking forward into the scene
    );
    uint64_t lastTime = SDL_GetTicks64();
    float deltaTime = 0.0f;

    float rotation = 0.0f;

    while (engine.IsRunning()) {
        uint64_t currentTime = SDL_GetTicks64();
        deltaTime = (currentTime - lastTime) / 1000.0f; // convert ms to seconds
        lastTime = currentTime;
        engine.BeginFrame();
        camera.Update(*engine.GetInput(), deltaTime);
        engine.GetRHI()->UpdateCamera(camera.GetViewMatrix(), camera.GetProjectionMatrix());

        // First box
        rotation += deltaTime * 45.0f; // 45 degrees per second
        engine.GetRHI()->SetModelMatrix(glm::rotate(glm::mat4(1.0f), glm::radians(rotation), glm::vec3(0.5f, 1.0f, 0.1f)));
        engine.GetRHI()->DrawIndexed(Box.indexCount);

        // Second box offset to the right
        engine.GetRHI()->SetModelMatrix(glm::translate(glm::mat4(1.0f), glm::vec3(2.0f, 0.0f, 0.0f)));
        engine.GetRHI()->DrawIndexed(Box.indexCount);

        engine.EndFrame();
    }
    engine.Shutdown();
    return 0;
}
