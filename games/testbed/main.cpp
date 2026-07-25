#define SDL_MAIN_HANDLED

#include "../../engine/core/Engine.h"

#include "renderer/MeshType.h"
#include "renderer/Camera.h"
#include "assets/MeshLoader.h"

#include <iostream>

#include <glm/gtc/matrix_transform.hpp>

#include "assets/SceneLoader.h"
#include "assets/TextureLoader.h"
#include "core/AssetManager.h"
#include "scene/Scene.h"


int main() {
    std::cout << "Osiris Engine" << std::endl;
    Osiris::Engine engine;

    if (!engine.Initialize()) {
        OSIRIS_ERROR("Failed to initialize engine!");
        return -1;
    }

    Osiris::Scene scene;
    Osiris::SceneLoader::Load(Osiris::AssetManager::GetPath("scenes/level_01.json"), scene, engine.GetRHI());

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
        scene.PreRender(engine.GetRHI()); // bind textures BEFORE BeginFrame
        engine.BeginFrame();
        camera.Update(*engine.GetInput(), deltaTime);
        engine.GetRHI()->UpdateCamera(camera.GetViewMatrix(), camera.GetProjectionMatrix());

        scene.Render(engine.GetRHI(), camera);

        engine.EndFrame();
    }
    engine.Shutdown();
    return 0;
}
