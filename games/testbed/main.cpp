#define SDL_MAIN_HANDLED

#include "../../engine/core/Engine.h"

#include "renderer/MeshType.h"
#include "renderer/Camera.h"
#include "assets/MeshLoader.h"

#include <iostream>

#include <glm/gtc/matrix_transform.hpp>

#include "imgui.h"
#include "assets/SceneLoader.h"
#include "assets/TextureLoader.h"
#include "core/AssetManager.h"
#include "renderer/RenderGraph.h"
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

    Osiris::RenderGraph graph;
    graph.AddPass("ShadowPass", Osiris::PassType::Graphics)
        .Write({Osiris::RGTexture{0}, Osiris::ResourceState::DepthWrite});

    graph.AddPass("ForwardPass", Osiris::PassType::Graphics)
        .Read({Osiris::RGTexture{0}, Osiris::ResourceState::ShaderRead})
        .Write({Osiris::RGTexture{1}, Osiris::ResourceState::ColorWrite});

    graph.AddPass("PresentPass", Osiris::PassType::Graphics)
        .Read({Osiris::RGTexture{1}, Osiris::ResourceState::Present});

    graph.Compile();

    Osiris::RenderGraph circularGraph;
    circularGraph.AddPass("PassA", Osiris::PassType::Graphics)
        .Read({Osiris::RGTexture{0}, Osiris::ResourceState::ShaderRead})
        .Write({Osiris::RGTexture{1}, Osiris::ResourceState::ColorWrite});

    circularGraph.AddPass("PassB", Osiris::PassType::Graphics)
        .Read({Osiris::RGTexture{1}, Osiris::ResourceState::ShaderRead})
        .Write({Osiris::RGTexture{0}, Osiris::ResourceState::ColorWrite});

    circularGraph.Compile();


    uint64_t lastCounter = SDL_GetPerformanceCounter();
    const double frequency = static_cast<double>(SDL_GetPerformanceFrequency());

    while (engine.IsRunning()) {
        const uint64_t currentCounter = SDL_GetPerformanceCounter();

        float deltaTime = static_cast<float>(
            (currentCounter - lastCounter) / frequency
        );

        lastCounter = currentCounter;

        engine.BeginFrame();

        camera.Update(*engine.GetInput(), deltaTime);
        engine.GetRHI()->UpdateCamera(camera.GetViewMatrix(), camera.GetProjectionMatrix());

        scene.Render(engine.GetRHI(), camera);

        // Stats panel
        ImGui::Begin("Osiris Engine");
        ImGui::Text("FPS: %.1f", 1.0f / deltaTime);
        ImGui::Text("Frame time: %.3f ms", deltaTime * 1000.0f);
        ImGui::Separator();

        // Camera
        ImGui::Text("Camera Position: %.2f %.2f %.2f",
            camera.GetPosition().x,
            camera.GetPosition().y,
            camera.GetPosition().z);
        ImGui::SliderFloat("Camera Speed", &camera.GetSpeed(), 0.1f, 20.0f);
        ImGui::Separator();

        // Scene
        ImGui::Text("Entities in scene: %d", scene.GetEntityCount());
        ImGui::Text("Draw calls: %d", scene.GetDrawCallCount());
        ImGui::Text("Culled: %d", scene.GetCulledCount());
        ImGui::End();

        engine.GetRHI()->RenderImGui();
        engine.EndFrame();
    }
    engine.Shutdown();
    return 0;
}
