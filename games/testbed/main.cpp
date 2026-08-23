#define SDL_MAIN_HANDLED

#include "../../engine/core/Engine.h"
#include "assets/MeshLoader.h"
#include "assets/TextureLoader.h"
#include "core/AssetManager.h"
#include "editor/Editor.h"
#include "renderer/Camera.h"
#include "scene/Scene.h"

#include <iostream>
#include <string>

namespace {
    constexpr const char* kTankModelPath = "models/toon_toy_tank/scene.gltf";
    constexpr const char* kTankControllerPath = "scripts/tank_controller.lua";
}

int main() {
    std::cout << "Osiris Engine" << std::endl;
    Osiris::Engine engine;

    if (!engine.Initialize()) {
        OSIRIS_ERROR("Failed to initialize engine!");
        return -1;
    }

    TextureHandle greyTexture = Osiris::TextureLoader::LoadFromFile(
        Osiris::AssetManager::GetPath("textures/grey.png"), engine.GetRHI());
    MaterialHandle greyMaterial = engine.GetRHI()->CreateMaterial({.albedo = greyTexture});

    Osiris::HDRImageData environmentHDR = Osiris::TextureLoader::LoadHDR(
        Osiris::AssetManager::GetPath("hdr/EveningRoad.hdr"));
    if (!environmentHDR.pixels.empty()) {
        engine.GetRHI()->LoadEnvironmentMap(
            environmentHDR.pixels.data(),
            static_cast<uint32_t>(environmentHDR.width),
            static_cast<uint32_t>(environmentHDR.height));
    }

    Osiris::Scene scene;

    auto addStaticBox = [&](const std::string& name, const glm::vec3& position,
                            const glm::vec3& halfExtents) {
        Osiris::Entity entity = scene.CreateEntity(name);
        entity.GetComponent<Osiris::TransformComponent>().position = position;
        entity.AddComponent<Osiris::MeshComponent>(
            Osiris::MeshLoader::CreateBox(halfExtents, engine.GetRHI()));
        entity.AddComponent<Osiris::MaterialComponent>(greyMaterial);
        entity.AddComponent<Osiris::ColliderComponent>(halfExtents);
        entity.AddComponent<Osiris::RigidBodyComponent>(Osiris::BodyMotionType::Static);
    };

    addStaticBox("Arena_Floor", {0.0f, -0.25f, 0.0f}, {9.0f, 0.25f, 7.0f});
    addStaticBox("Arena_Wall_North", {0.0f, 0.75f, -7.25f}, {9.25f, 0.75f, 0.25f});
    addStaticBox("Arena_Wall_South", {0.0f, 0.75f, 7.25f}, {9.25f, 0.75f, 0.25f});
    addStaticBox("Arena_Wall_East", {9.25f, 0.75f, 0.0f}, {0.25f, 0.75f, 7.5f});
    addStaticBox("Arena_Wall_West", {-9.25f, 0.75f, 0.0f}, {0.25f, 0.75f, 7.5f});
    addStaticBox("Arena_Obstacle_North", {0.0f, 0.6f, -3.5f}, {1.5f, 0.6f, 0.5f});
    addStaticBox("Arena_Obstacle_South", {0.0f, 0.6f, 3.5f}, {1.5f, 0.6f, 0.5f});

    auto spawnTank = [&](const std::string& name, const glm::vec3& position, float yaw) {
        Osiris::Entity hull = scene.SpawnModel(name, kTankModelPath, engine.GetRHI());
        if (!hull.IsValid()) {
            OSIRIS_ERROR("Failed to spawn tank model for {}", name);
            return;
        }

        auto& transform = hull.GetComponent<Osiris::TransformComponent>();
        transform.position = position;
        transform.rotation.y = yaw;
        hull.AddComponent<Osiris::ColliderComponent>(glm::vec3(0.85f, 0.55f, 0.95f));
        auto& rigidBody = hull.AddComponent<Osiris::RigidBodyComponent>(Osiris::BodyMotionType::Dynamic);
        rigidBody.lockRotationToYAxis = true;
        hull.AddComponent<Osiris::ScriptComponent>().scriptPath =
            Osiris::AssetManager::GetPath(kTankControllerPath);
    };

    spawnTank("Player1_Tank", {-3.5f, 0.6f, 0.0f}, -90.0f);
    spawnTank("Player2_Tank", {3.5f, 0.6f, 0.0f}, 90.0f);

    scene.CreatePhysicsBodies(engine.GetPhysics());
    scene.CreateScriptInstances(engine.GetScripting());

    Osiris::Camera editorCamera(
        glm::vec3(0.0f, 1.5f, 4.0f),
        glm::vec3(0.0f, 0.0f, -1.0f));
    const glm::vec3 tankCameraPosition(0.0f, 14.0f, 10.0f);
    Osiris::Camera tankCamera(tankCameraPosition, glm::vec3(0.0f, 0.0f, -1.0f));
    tankCamera.SetOrientation(
        glm::normalize(glm::vec3(0.0f) - tankCameraPosition),
        glm::vec3(0.0f, 1.0f, 0.0f));

    Osiris::Editor editor;

    uint64_t lastCounter = SDL_GetPerformanceCounter();
    const double frequency = static_cast<double>(SDL_GetPerformanceFrequency());

    while (engine.IsRunning()) {
        const uint64_t currentCounter = SDL_GetPerformanceCounter();
        const float deltaTime = static_cast<float>((currentCounter - lastCounter) / frequency);
        lastCounter = currentCounter;

        engine.BeginFrame();
        editor.BeginFrame();

        if (engine.GetInput()->IsKeyPressed(SDL_SCANCODE_F5)) {
            if (engine.IsPlaying()) engine.ExitPlayMode(scene);
            else engine.EnterPlayMode(scene);
        }

        if (!engine.IsPlaying()) {
            editor.Draw(scene, editorCamera, engine, deltaTime);
        } else {
            // Scripts (tank_controller.lua) read last frame's synced Transform and call
            // physics:SetBodyVelocity before the physics step consumes it, same ordering the old
            // C++ character controller used: set desired velocity, then step, then sync.
            engine.GetScripting()->Update(deltaTime);
            engine.GetScripting()->FixedUpdate(deltaTime);
            engine.GetPhysics()->Update(deltaTime);
            scene.SyncPhysicsTransforms(engine.GetPhysics());
            scene.DispatchCollisionEvents(engine.GetPhysics(), engine.GetScripting());
            scene.FlushDestroyQueue(engine.GetPhysics(), engine.GetAudio(), engine.GetScripting());
        }

        Osiris::Camera& renderCamera = engine.IsPlaying() ? tankCamera : editorCamera;
        engine.RenderFrame(scene, renderCamera, editor.IsDebugLightViewEnabled(), editor.GetDebugCascade());
        engine.RenderImGui();
        engine.EndFrame();
    }

    engine.Shutdown();
    return 0;
}
