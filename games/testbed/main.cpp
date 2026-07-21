#define SDL_MAIN_HANDLED

#include "../../engine/core/Engine.h"

#include "renderer/MeshType.h"
#include "renderer/Camera.h"
#include "assets/MeshLoader.h"

#include <iostream>



int main() {
    std::cout << "Osiris Engine" << std::endl;
    Osiris::Engine engine;

    if (!engine.Initialize()) {
        OSIRIS_ERROR("Failed to initialize engine!");
        return -1;
    }

    Osiris::Mesh Box = Osiris::MeshLoader::LoadFromGLTF("C:/Dev/Osiris/assets/models/BoxGLTF/Box.gltf", engine.GetRHI());

    engine.GetRHI()->SetMeshData(Box);

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
