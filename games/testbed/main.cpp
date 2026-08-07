#define SDL_MAIN_HANDLED

#include "../../engine/core/Engine.h"
#include "renderer/MeshType.h"
#include "renderer/Camera.h"
#include "assets/MeshLoader.h"
#include "assets/SceneLoader.h"
#include "assets/TextureLoader.h"
#include "core/AssetManager.h"
#include "scene/Scene.h"

#include <iostream>
#include <glm/gtc/matrix_transform.hpp>
#include "imgui.h"

int main() {
    std::cout << "Osiris Engine" << std::endl;
    Osiris::Engine engine;

    if (!engine.Initialize()) {
        OSIRIS_ERROR("Failed to initialize engine!");
        return -1;
    }

    // Create plane mesh
    Osiris::Mesh wallPlane  = Osiris::MeshLoader::CreatePlane(10.0f, 3.0f, engine.GetRHI());
    Osiris::Mesh floorPlane = Osiris::MeshLoader::CreatePlane(10.0f, 10.0f, engine.GetRHI());

    // Load box mesh and texture
    Osiris::Mesh box = Osiris::MeshLoader::LoadFromGLTF(
        Osiris::AssetManager::GetPath("models/BoxTexturedGLTF/BoxTextured.gltf"), engine.GetRHI());
    TextureHandle boxTexture = Osiris::TextureLoader::LoadFromFile(
        Osiris::AssetManager::GetPath("textures/box.png"), engine.GetRHI());

    // Create a grey texture for walls/floor/ceiling
    TextureHandle greyTexture = Osiris::TextureLoader::LoadFromFile(
        Osiris::AssetManager::GetPath("textures/grey.png"), engine.GetRHI());

    // Create materials
    MaterialHandle boxMaterial  = engine.GetRHI()->CreateMaterial({.albedo = boxTexture});
    MaterialHandle greyMaterial = engine.GetRHI()->CreateMaterial({.albedo = greyTexture});

    Osiris::Scene scene;

    // Floor

    // Ceiling
    Osiris::Entity ceiling = scene.CreateEntity("Ceiling");
    ceiling.GetComponent<Osiris::TransformComponent>().position = glm::vec3(0.0f, 3.0f, 0.0f);
    ceiling.GetComponent<Osiris::TransformComponent>().rotation = glm::vec3(180.0f, 0.0f, 0.0f);
    ceiling.AddComponent<Osiris::MeshComponent>(floorPlane);
    ceiling.AddComponent<Osiris::MaterialComponent>(greyMaterial);

    // Wall North (Z-)
    Osiris::Entity wallNorth = scene.CreateEntity("Wall_North");
    wallNorth.GetComponent<Osiris::TransformComponent>().position = glm::vec3(0.0f, 1.5f, -5.0f);
    wallNorth.GetComponent<Osiris::TransformComponent>().rotation = glm::vec3(90.0f, 0.0f, 0.0f);
    wallNorth.AddComponent<Osiris::MeshComponent>(wallPlane);
    wallNorth.AddComponent<Osiris::MaterialComponent>(greyMaterial);

    // Wall South (Z+)
    Osiris::Entity wallSouth = scene.CreateEntity("Wall_South");
    wallSouth.GetComponent<Osiris::TransformComponent>().position = glm::vec3(0.0f, 1.5f, 5.0f);
    wallSouth.GetComponent<Osiris::TransformComponent>().rotation = glm::vec3(-90.0f, 0.0f, 0.0f);
    wallSouth.AddComponent<Osiris::MeshComponent>(wallPlane);
    wallSouth.AddComponent<Osiris::MaterialComponent>(greyMaterial);

    // Wall East (X+)
    Osiris::Entity wallEast = scene.CreateEntity("Wall_East");
    wallEast.GetComponent<Osiris::TransformComponent>().position = glm::vec3(5.0f, 1.5f, 0.0f);
    wallEast.GetComponent<Osiris::TransformComponent>().rotation = glm::vec3(90.0f, 0.0f, 90.0f);
    wallEast.AddComponent<Osiris::MeshComponent>(wallPlane);
    wallEast.AddComponent<Osiris::MaterialComponent>(greyMaterial);

    // Wall West (X-)
    Osiris::Entity wallWest = scene.CreateEntity("Wall_West");
    wallWest.GetComponent<Osiris::TransformComponent>().position = glm::vec3(-5.0f, 1.5f, 0.0f);
    wallWest.GetComponent<Osiris::TransformComponent>().rotation = glm::vec3(-90.0f, 0.0f, -90.0f);
    wallWest.AddComponent<Osiris::MeshComponent>(wallPlane);
    wallWest.AddComponent<Osiris::MaterialComponent>(greyMaterial);

    // Crates
    Osiris::Entity crate1 = scene.CreateEntity("Crate_01");
    crate1.GetComponent<Osiris::TransformComponent>().position = glm::vec3(-1.0f, 1.0f, -1.0f);
    crate1.AddComponent<Osiris::MeshComponent>(box);
    crate1.AddComponent<Osiris::MaterialComponent>(boxMaterial);

    Osiris::Entity crate2 = scene.CreateEntity("Crate_02");
    crate2.GetComponent<Osiris::TransformComponent>().position = glm::vec3(1.5f, -0.5f, 0.5f);
    crate2.GetComponent<Osiris::TransformComponent>().rotation = glm::vec3(0.0f, 45.0f, 0.0f);
    crate2.AddComponent<Osiris::MeshComponent>(box);
    crate2.AddComponent<Osiris::MaterialComponent>(boxMaterial);

    Osiris::Camera camera(
        glm::vec3(0.0f, 1.5f, 10.0f),
        glm::vec3(0.0f, 0.0f, -1.0f)   // looking north
    );

    uint64_t lastCounter = SDL_GetPerformanceCounter();
    const double frequency = static_cast<double>(SDL_GetPerformanceFrequency());
    float rotation = 0.0f;

    while (engine.IsRunning()) {
        const uint64_t currentCounter = SDL_GetPerformanceCounter();
        float deltaTime = static_cast<float>((currentCounter - lastCounter) / frequency);
        lastCounter = currentCounter;

        rotation += 45.0f * deltaTime;
        crate1.GetComponent<Osiris::TransformComponent>().rotation = glm::vec3(rotation, rotation, rotation);

        engine.BeginFrame();

        camera.Update(*engine.GetInput(), deltaTime);
        engine.GetRHI()->UpdateCamera(camera.GetViewMatrix(), camera.GetProjectionMatrix());

        // Shadow passes — render scene from light's perspective for each cascade
        for (uint32_t i = 0; i < 3; i++) {
            engine.GetRHI()->BeginShadowPass(i);
            scene.RenderShadows(engine.GetRHI());
            engine.GetRHI()->EndShadowPass(i);
        }

        // Forward pass
        engine.GetRHI()->BeginForwardPass();
        scene.Render(engine.GetRHI(), camera);

        // ImGui
        ImGui::Begin("Osiris Engine");
        ImGui::Text("FPS: %.1f", deltaTime > 0.0f ? 1.0f / deltaTime : 0.0f);
        ImGui::Text("Frame time: %.3f ms", deltaTime * 1000.0f);
        ImGui::Separator();
        ImGui::Text("Camera: %.2f %.2f %.2f",
            camera.GetPosition().x, camera.GetPosition().y, camera.GetPosition().z);
        ImGui::SliderFloat("Camera Speed", &camera.GetSpeed(), 0.1f, 20.0f);
        ImGui::Separator();
        ImGui::Text("Draw calls: %d", scene.GetDrawCallCount());
        ImGui::Text("Culled: %d", scene.GetCulledCount());
        ImGui::End();

        engine.GetRHI()->RenderImGui();
        engine.EndFrame();
    }

    engine.Shutdown();
    return 0;
}