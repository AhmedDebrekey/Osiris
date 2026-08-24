#define SDL_MAIN_HANDLED

#include "../../engine/core/Engine.h"
#include "assets/SceneLoader.h"
#include "assets/TextureLoader.h"
#include "core/AssetManager.h"
#include "scene/Scene.h"

#include <iostream>
#include <string>

namespace {
    constexpr const char* kTankModelPath = "models/toon_toy_tank/scene.gltf";
    constexpr const char* kTankControllerPath = "scripts/tank_controller.lua";
    constexpr const char* kFlagModelPath = "models/Flag/Flag.gltf";
    constexpr const char* kFlagScriptPath = "scripts/flag.lua";
}

int main() {
    std::cout << "Osiris Engine" << std::endl;
    Osiris::Engine engine;

    if (!engine.Initialize()) {
        OSIRIS_ERROR("Failed to initialize engine!");
        return -1;
    }

    Osiris::HDRImageData environmentHDR = Osiris::TextureLoader::LoadHDR(
        Osiris::AssetManager::GetPath("hdr/EveningRoad.hdr"));
    if (!environmentHDR.pixels.empty()) {
        engine.GetRHI()->LoadEnvironmentMap(
            environmentHDR.pixels.data(),
            static_cast<uint32_t>(environmentHDR.width),
            static_cast<uint32_t>(environmentHDR.height));
    }

    Osiris::Scene scene;
    Osiris::SceneLoader::Load(
        Osiris::AssetManager::GetPath("scenes/sandbox_standoff.json"),
        scene, engine.GetRHI(), engine.GetAudio());

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

    auto spawnFlag = [&](const std::string& name, const glm::vec3& position) {
        Osiris::Entity flag = scene.SpawnModel(name, kFlagModelPath, engine.GetRHI());
        if (!flag.IsValid()) {
            OSIRIS_ERROR("Failed to spawn flag model for {}", name);
            return;
        }

        auto& transform = flag.GetComponent<Osiris::TransformComponent>();
        transform.position = position;
        transform.scale = glm::vec3(0.3f);
        flag.AddComponent<Osiris::ColliderComponent>(glm::vec3(0.4f, 0.6f, 0.4f));
        auto& rigidBody = flag.AddComponent<Osiris::RigidBodyComponent>(Osiris::BodyMotionType::Static);
        rigidBody.isSensor = true;
        flag.AddComponent<Osiris::ScriptComponent>().scriptPath =
            Osiris::AssetManager::GetPath(kFlagScriptPath);
    };

    spawnTank("Player1_Tank", {-6.0f, 0.6f, 0.0f}, -90.0f);
    spawnTank("Player2_Tank", {6.0f, 0.6f, 0.0f}, 90.0f);
    spawnFlag("P1_Flag", {-9.5f, 0.6f, 0.0f});
    spawnFlag("P2_Flag", {9.5f, 0.6f, 0.0f});

    scene.CreatePhysicsBodies(engine.GetPhysics());
    scene.CreateScriptInstances(engine.GetScripting());

    while (engine.IsRunning()) {
        engine.RunFrame(scene);
    }

    engine.Shutdown();
    return 0;
}
