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
#include "renderer/Light.h"

int main() {
    std::cout << "Osiris Engine" << std::endl;
    Osiris::Engine engine;

    if (!engine.Initialize()) {
        OSIRIS_ERROR("Failed to initialize engine!");
        return -1;
    }

    // Create plane meshes
    Osiris::Mesh wallPlane  = Osiris::MeshLoader::CreatePlane(10.0f, 3.0f, engine.GetRHI());
    Osiris::Mesh floorPlane = Osiris::MeshLoader::CreatePlane(10.0f, 10.0f, engine.GetRHI());

    // Load box mesh + texture
    auto boxPrimitives = Osiris::MeshLoader::LoadFromGLTF(
        Osiris::AssetManager::GetPath("models/BoxTexturedGLTF/BoxTextured.gltf"), engine.GetRHI());

    TextureHandle greyTexture = Osiris::TextureLoader::LoadFromFile(
        Osiris::AssetManager::GetPath("textures/grey.png"), engine.GetRHI());
    MaterialHandle greyMaterial = engine.GetRHI()->CreateMaterial({.albedo = greyTexture});

    Osiris::Scene scene;


    // Ceiling
    Osiris::Entity ceiling = scene.CreateEntity("Ceiling");
    ceiling.GetComponent<Osiris::TransformComponent>().position = glm::vec3(0.0f, 0.0f, 0.0f);
    ceiling.GetComponent<Osiris::TransformComponent>().rotation = glm::vec3(0.0f, 0.0f, 0.0f);
    ceiling.AddComponent<Osiris::MeshComponent>(floorPlane);
    ceiling.AddComponent<Osiris::MaterialComponent>(greyMaterial);

    // Walls
    Osiris::Entity wallNorth = scene.CreateEntity("Wall_North");
    wallNorth.GetComponent<Osiris::TransformComponent>().position = glm::vec3(0.0f, 1.5f, -5.0f);
    wallNorth.GetComponent<Osiris::TransformComponent>().rotation = glm::vec3(90.0f, 0.0f, 0.0f);
    wallNorth.AddComponent<Osiris::MeshComponent>(wallPlane);
    wallNorth.AddComponent<Osiris::MaterialComponent>(greyMaterial);

    Osiris::Entity wallSouth = scene.CreateEntity("Wall_South");
    wallSouth.GetComponent<Osiris::TransformComponent>().position = glm::vec3(0.0f, 1.5f, 5.0f);
    wallSouth.GetComponent<Osiris::TransformComponent>().rotation = glm::vec3(-90.0f, 0.0f, 0.0f);
    wallSouth.AddComponent<Osiris::MeshComponent>(wallPlane);
    wallSouth.AddComponent<Osiris::MaterialComponent>(greyMaterial);

    Osiris::Entity wallEast = scene.CreateEntity("Wall_East");
    wallEast.GetComponent<Osiris::TransformComponent>().position = glm::vec3(5.0f, 1.5f, 0.0f);
    wallEast.GetComponent<Osiris::TransformComponent>().rotation = glm::vec3(90.0f, 0.0f, 90.0f);
    wallEast.AddComponent<Osiris::MeshComponent>(wallPlane);
    wallEast.AddComponent<Osiris::MaterialComponent>(greyMaterial);

    Osiris::Entity wallWest = scene.CreateEntity("Wall_West");
    wallWest.GetComponent<Osiris::TransformComponent>().position = glm::vec3(-5.0f, 1.5f, 0.0f);
    wallWest.GetComponent<Osiris::TransformComponent>().rotation = glm::vec3(-90.0f, 0.0f, -90.0f);
    wallWest.AddComponent<Osiris::MeshComponent>(wallPlane);
    wallWest.AddComponent<Osiris::MaterialComponent>(greyMaterial);

    // Crates
    for (uint32_t i = 0; i < boxPrimitives.size(); i++) {
        Osiris::Entity crate1 = scene.CreateEntity("Crate_01_" + std::to_string(i));
        crate1.GetComponent<Osiris::TransformComponent>().position = glm::vec3(-1.0f, 1.5f, -1.0f);
        crate1.AddComponent<Osiris::MeshComponent>(boxPrimitives[i].mesh);
        crate1.AddComponent<Osiris::MaterialComponent>(boxPrimitives[i].material);

        Osiris::Entity crate2 = scene.CreateEntity("Crate_02_" + std::to_string(i));
        crate2.GetComponent<Osiris::TransformComponent>().position = glm::vec3(1.5f, 2.5f, 0.5f);
        crate2.GetComponent<Osiris::TransformComponent>().rotation = glm::vec3(0.0f, 45.0f, 0.0f);
        crate2.AddComponent<Osiris::MeshComponent>(boxPrimitives[i].mesh);
        crate2.AddComponent<Osiris::MaterialComponent>(boxPrimitives[i].material);
    }

    Osiris::Camera camera(
        glm::vec3(0.0f, 1.5f, 4.0f),
        glm::vec3(0.0f, 0.0f, -1.0f)
    );

    uint64_t lastCounter = SDL_GetPerformanceCounter();
    const double frequency = static_cast<double>(SDL_GetPerformanceFrequency());

    static bool debugLightView = false;
    static int debugCascade = 0;

    while (engine.IsRunning()) {
        const uint64_t currentCounter = SDL_GetPerformanceCounter();
        float deltaTime = static_cast<float>((currentCounter - lastCounter) / frequency);
        lastCounter = currentCounter;

        engine.BeginFrame();

        camera.Update(*engine.GetInput(), deltaTime);

        glm::mat4 lightView, lightProj;
        engine.GetRHI()->UpdateCamera(camera.GetViewMatrix(), camera.GetProjectionMatrix(), glm::vec4(camera.GetPosition(), 0.0f), camera.GetFront());

        if (debugLightView) {
            lightView = engine.GetRHI()->GetLightViewMatrix(debugCascade);
            lightProj = engine.GetRHI()->GetLightProjMatrix(debugCascade);
            engine.GetRHI()->SetCameraBuffer(lightView, lightProj, glm::vec4(camera.GetPosition(), 0.0f));
        }

        // Shadow passes

        // Directional cascade shadow passes
        for (uint32_t i = 0; i < 3; i++) {
            engine.GetRHI()->BeginShadowPass(i);
            scene.RenderShadows(engine.GetRHI());
            engine.GetRHI()->EndShadowPass(i);
        }

        // Spot light shadow pass
        engine.GetRHI()->BeginSpotShadowPass();
        scene.RenderShadows(engine.GetRHI());
        engine.GetRHI()->EndSpotShadowPass();

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
        ImGui::Separator();

        auto& light = engine.GetRHI()->GetDirectionalLight();
        ImGui::SliderFloat3("Light Direction", &light.direction.x, -1.0f, 1.0f);
        light.direction = glm::normalize(light.direction);
        if (glm::abs(light.direction.y) > 0.999f) {
            light.direction.y = glm::sign(light.direction.y) * 0.999f;
            light.direction = glm::normalize(light.direction);
        }
        ImGui::ColorEdit3("Light Color", &light.color.x);
        ImGui::SliderFloat("Light Intensity", &light.intensity, 0.0f, 5.0f);
        ImGui::Separator();

        auto& spotLight = engine.GetRHI()->GetSpotLight();
        ImGui::SliderFloat3("Spot Position", &spotLight.position.x, -10.0f, 10.0f);
        ImGui::SliderFloat3("Spot Direction", &spotLight.direction.x, -1.0f, 1.0f);
        spotLight.direction = glm::normalize(spotLight.direction);
        ImGui::ColorEdit3("Spot Color", &spotLight.color.x);
        ImGui::SliderFloat("Spot Intensity", &spotLight.intensity, 0.0f, 50.0f);
        ImGui::SliderFloat("Spot Inner Cone", &spotLight.innerConeDegrees, 1.0f, 89.0f);
        ImGui::SliderFloat("Spot Outer Cone", &spotLight.outerConeDegrees, 1.0f, 89.0f);
        if (spotLight.outerConeDegrees < spotLight.innerConeDegrees) {
            spotLight.outerConeDegrees = spotLight.innerConeDegrees;
        }
        ImGui::SliderFloat("Spot Range", &spotLight.range, 1.0f, 50.0f);
        ImGui::Separator();

        auto& shadowSettings = engine.GetRHI()->GetShadowSettings();
        ImGui::SliderFloat("Shadow Near", &shadowSettings.nearClip, 0.01f, 5.0f);
        ImGui::SliderFloat("Shadow Far", &shadowSettings.farClip, 5.0f, 50.0f);
        ImGui::SliderFloat("Cascade Lambda", &shadowSettings.cascadeSplitLambda, 0.0f, 1.0f);
        ImGui::Separator();

        ImGui::Checkbox("Debug: Light View", &debugLightView);
        if (debugLightView) {
            ImGui::SliderInt("Cascade", &debugCascade, 0, 2);
        }

        if (ImGui::CollapsingHeader("Shadow Debug")) {
            for (int cascade = 0; cascade < 3; cascade++) {
                glm::mat4 m = engine.GetRHI()->GetLightSpaceMatrix(cascade);
                ImGui::Text("Cascade %d Light Space Matrix:", cascade);
                for (int row = 0; row < 4; row++) {
                    ImGui::Text("%.3f %.3f %.3f %.3f",
                        m[0][row], m[1][row], m[2][row], m[3][row]);
                }
                ImGui::Separator();
            }
        }
        ImGui::End();

        engine.GetRHI()->RenderImGui();
        engine.EndFrame();
    }

    engine.Shutdown();
    return 0;
}