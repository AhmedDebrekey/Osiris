#define SDL_MAIN_HANDLED

#include "../../engine/core/Engine.h"
#include "renderer/MeshType.h"
#include "renderer/Camera.h"
#include "assets/MeshLoader.h"
#include "assets/SceneLoader.h"
#include "assets/TextureLoader.h"
#include "core/AssetManager.h"
#include "scene/Scene.h"
#include "physics/IPhysics.h"
#include "editor/SceneInspectorPanel.h"
#include "audio/IAudio.h"
#include "assets/AudioLoader.h"

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

    // Phase 7A checkpoint: a minimal cube-only scene to test static colliders + the character
    // controller in isolation, without Sponza/DamagedHelmet's mesh weight in the way. Flip this
    // back to false to restore the full room scene below.
    constexpr bool kPhysicsTestScene = true;

    TextureHandle greyTexture = Osiris::TextureLoader::LoadFromFile(
        Osiris::AssetManager::GetPath("textures/grey.png"), engine.GetRHI());
    MaterialHandle greyMaterial = engine.GetRHI()->CreateMaterial({.albedo = greyTexture});

    // IBL environment (Phase 6D). One-shot: builds the environment cubemap
    // once at startup, not part of the per-frame loop.
    Osiris::HDRImageData environmentHDR = Osiris::TextureLoader::LoadHDR(
        Osiris::AssetManager::GetPath("hdr/EveningRoad.hdr"));
    if (!environmentHDR.pixels.empty()) {
        engine.GetRHI()->LoadEnvironmentMap(
            environmentHDR.pixels.data(),
            static_cast<uint32_t>(environmentHDR.width),
            static_cast<uint32_t>(environmentHDR.height));
    }

    // Audio (Phase 7B). Drop a test file at assets/audio/test.wav before building.
    Osiris::PCMAudioData testAudio = Osiris::AudioLoader::LoadWAV(
        Osiris::AssetManager::GetPath("audio/test.wav"));
    Osiris::AudioBufferHandle testSoundBuffer;
    if (!testAudio.pcmData.empty()) {
        testSoundBuffer = engine.GetAudio()->CreateBuffer(testAudio);
    }

    Osiris::Scene scene;

    // Phase 7B checkpoint 2: a looping 3D emitter positioned away from the player's spawn, so
    // walking toward/away from it is an easy way to hear distance attenuation/panning working.
    // 'P' toggles it on/off during testing.
    Osiris::Entity audioTestEntity;
    if (testSoundBuffer.IsValid()) {
        audioTestEntity = scene.CreateEntity("AudioTest_Emitter");
        audioTestEntity.GetComponent<Osiris::TransformComponent>().position = glm::vec3(0.0f, 1.0f, -2.0f);
        auto& audioSrc = audioTestEntity.AddComponent<Osiris::AudioSourceComponent>();
        audioSrc.clip              = testSoundBuffer;
        audioSrc.loop              = true;
        audioSrc.autoPlay          = true;
        audioSrc.gain              = 0.6f;
        audioSrc.referenceDistance = 1.5f;
        audioSrc.maxDistance       = 15.0f;

        // Scripting checkpoint 2: proves the input/audio globals by toggling this same emitter
        // from Lua (press E) instead of the hardcoded 'P' key below.
        audioTestEntity.AddComponent<Osiris::ScriptComponent>().scriptPath =
            Osiris::AssetManager::GetPath("scripts/audio_trigger.lua");
    }


    // Player character (Phase 7A). Feet start a little above the ground box so it's visibly
    // dropped and settled by gravity/collision on the first few frames, not just spawned resting.
    Osiris::CharacterHandle playerCharacter;
    constexpr float kPlayerEyeHeight = 1.5f;
    if (kPhysicsTestScene) {
        playerCharacter = engine.GetPhysics()->CreateCharacter({
            .radius = 0.3f,
            .height = 1.6f,
            .position = glm::vec3(0.0f, 1.0f, 3.0f),
        });
    }

    if (kPhysicsTestScene) {
        // Ground: a wide, flat static box. Top surface sits at y=0.
        Osiris::Mesh groundMesh = Osiris::MeshLoader::CreateBox({10.0f, 0.5f, 10.0f}, engine.GetRHI());
        Osiris::Entity ground = scene.CreateEntity("Ground");
        ground.GetComponent<Osiris::TransformComponent>().position = glm::vec3(0.0f, -0.5f, 0.0f);
        ground.AddComponent<Osiris::MeshComponent>(groundMesh);
        ground.AddComponent<Osiris::MaterialComponent>(greyMaterial);
        ground.AddComponent<Osiris::ColliderComponent>(glm::vec3(10.0f, 0.5f, 10.0f));
        ground.AddComponent<Osiris::RigidBodyComponent>(Osiris::BodyMotionType::Static);

        // A few static obstacle boxes to walk into and verify collision response.
        struct Obstacle { glm::vec3 position; glm::vec3 halfExtents; };
        const Obstacle obstacles[] = {
            { {-2.0f, 0.5f, -2.0f}, {0.5f, 0.5f, 0.5f} },
            { { 2.0f, 0.75f, -1.0f}, {0.75f, 0.75f, 0.75f} },
            { { 0.0f, 0.4f,  2.5f}, {1.0f, 0.4f, 0.4f} },
        };
        for (size_t i = 0; i < sizeof(obstacles) / sizeof(obstacles[0]); i++) {
            Osiris::Mesh boxMesh = Osiris::MeshLoader::CreateBox(obstacles[i].halfExtents, engine.GetRHI());
            Osiris::Entity obstacle = scene.CreateEntity("Obstacle_" + std::to_string(i));
            obstacle.GetComponent<Osiris::TransformComponent>().position = obstacles[i].position;
            obstacle.AddComponent<Osiris::MeshComponent>(boxMesh);
            obstacle.AddComponent<Osiris::MaterialComponent>(greyMaterial);
            obstacle.AddComponent<Osiris::ColliderComponent>(obstacles[i].halfExtents);
            obstacle.AddComponent<Osiris::RigidBodyComponent>(Osiris::BodyMotionType::Static);

            // Scripting checkpoint 1: attach a self-manipulating script to the first obstacle to
            // prove out OnStart/OnUpdate/OnFixedUpdate + live TransformComponent mutation from Lua.
            if (i == 0) {
                obstacle.AddComponent<Osiris::ScriptComponent>().scriptPath =
                    Osiris::AssetManager::GetPath("scripts/spin_cube.lua");
            }
        }

        // A few dynamic boxes dropped above the obstacles, to check that bodies actually fall,
        // tumble, collide, and settle — position + rotation are synced back to
        // TransformComponent every frame via Scene::SyncPhysicsTransforms.
        const glm::vec3 dropHalfExtents(0.35f, 0.35f, 0.35f);
        const glm::vec3 dropPositions[] = {
            { 0.0f, 4.0f, 0.0f },
            { 0.3f, 5.0f, 0.2f },
            {-0.2f, 6.0f, -0.1f },
        };
        for (size_t i = 0; i < sizeof(dropPositions) / sizeof(dropPositions[0]); i++) {
            Osiris::Mesh boxMesh = Osiris::MeshLoader::CreateBox(dropHalfExtents, engine.GetRHI());
            Osiris::Entity dropBox = scene.CreateEntity("DropBox_" + std::to_string(i));
            dropBox.GetComponent<Osiris::TransformComponent>().position = dropPositions[i];
            dropBox.AddComponent<Osiris::MeshComponent>(boxMesh);
            dropBox.AddComponent<Osiris::MaterialComponent>(greyMaterial);
            dropBox.AddComponent<Osiris::ColliderComponent>(dropHalfExtents);
            dropBox.AddComponent<Osiris::RigidBodyComponent>(Osiris::BodyMotionType::Dynamic);
        }

    } else {
        Osiris::Mesh wallPlane  = Osiris::MeshLoader::CreatePlane(10.0f, 3.0f, engine.GetRHI());
        Osiris::Mesh floorPlane = Osiris::MeshLoader::CreatePlane(10.0f, 10.0f, engine.GetRHI());
        auto boxPrimitives = Osiris::MeshLoader::LoadFromGLTF(
            Osiris::AssetManager::GetPath("models/BoxTexturedGLTF/BoxTextured.gltf"), engine.GetRHI());

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

        // PBR test model (Khronos DamagedHelmet) — has real metallic/roughness
        // variation, unlike the fully-diffuse crates, so it's a much better way
        // to visually verify specular IBL once tasks 4-6 land.
        auto helmetPrimitives = Osiris::MeshLoader::LoadFromGLTF(
            Osiris::AssetManager::GetPath("models/DamagedHelmet/DamagedHelmet.gltf"), engine.GetRHI());
        for (uint32_t i = 0; i < helmetPrimitives.size(); i++) {
            Osiris::Entity helmet = scene.CreateEntity("DamagedHelmet_" + std::to_string(i));
            helmet.GetComponent<Osiris::TransformComponent>().position = glm::vec3(-2.5f, 1.3f, -0.5f);
            helmet.AddComponent<Osiris::MeshComponent>(helmetPrimitives[i].mesh);
            helmet.AddComponent<Osiris::MaterialComponent>(helmetPrimitives[i].material);
        }

        auto sponzaPrimitives = Osiris::MeshLoader::LoadFromGLTF(
            Osiris::AssetManager::GetPath("models/sponza/Sponza.gltf"), engine.GetRHI());
        for (uint32_t i = 0; i < sponzaPrimitives.size(); i++) {
            Osiris::Entity sponza = scene.CreateEntity("sponza_" + std::to_string(i));
            sponza.AddComponent<Osiris::MeshComponent>(sponzaPrimitives[i].mesh);
            sponza.AddComponent<Osiris::MaterialComponent>(sponzaPrimitives[i].material);
        }
    }

    scene.CreatePhysicsBodies(engine.GetPhysics());
    scene.CreateAudioSources(engine.GetAudio());

    // Spotlights: TransformComponent + SpotLightComponent.
    Osiris::Entity spotLight1 = scene.CreateEntity("SpotLight_Ceiling");
    spotLight1.GetComponent<Osiris::TransformComponent>().position = glm::vec3(-2.5f, 5.0f, 1.5f);
    spotLight1.GetComponent<Osiris::TransformComponent>().rotation = glm::vec3(25.0f, 0.0f, 0.0f);
    auto& spotLight1Comp = spotLight1.AddComponent<Osiris::SpotLightComponent>();
    spotLight1Comp.color        = glm::vec3(1.0f, 0.9f, 0.75f);
    spotLight1Comp.intensity    = 30.0f;
    spotLight1Comp.range        = 10.0f;
    spotLight1Comp.castsShadow  = true;

    Osiris::Entity spotLight2 = scene.CreateEntity("SpotLight_Corner");
    spotLight2.GetComponent<Osiris::TransformComponent>().position = glm::vec3(4.0f, 2.9f, 4.0f);
    spotLight2.GetComponent<Osiris::TransformComponent>().rotation = glm::vec3(0.0f, 0.0f, 20.0f);
    auto& spotLight2Comp = spotLight2.AddComponent<Osiris::SpotLightComponent>();
    spotLight2Comp.color        = glm::vec3(0.7f, 0.85f, 1.0f);
    spotLight2Comp.intensity    = 15.0f;
    spotLight2Comp.range        = 7.0f;
    spotLight2Comp.castsShadow  = true;

    Osiris::Entity spotLight3 = scene.CreateEntity("SpotLight_Accent");
    spotLight3.GetComponent<Osiris::TransformComponent>().position = glm::vec3(1.5f, 2.9f, 0.5f);
    auto& spotLight3Comp = spotLight3.AddComponent<Osiris::SpotLightComponent>();
    spotLight3Comp.color        = glm::vec3(1.0f, 0.4f, 0.3f);
    spotLight3Comp.intensity    = 10.0f;
    spotLight3Comp.range        = 5.0f;
    spotLight3Comp.castsShadow  = false; // demonstrates a non-shadow-casting spot light

    // Scripting checkpoint 1: a headless (no mesh) controller entity whose script reaches into
    // SpotLight_Accent above via scene:FindEntityByName — proves cross-entity component access.
    Osiris::Entity scriptController = scene.CreateEntity("ScriptController");
    scriptController.AddComponent<Osiris::ScriptComponent>().scriptPath =
        Osiris::AssetManager::GetPath("scripts/pulse_light.lua");

    scene.CreateScriptInstances(engine.GetScripting());

    Osiris::Camera camera(
        glm::vec3(0.0f, 1.5f, 4.0f),
        glm::vec3(0.0f, 0.0f, -1.0f)
    );

    Osiris::SceneInspectorPanel sceneInspector;

    uint64_t lastCounter = SDL_GetPerformanceCounter();
    const double frequency = static_cast<double>(SDL_GetPerformanceFrequency());

    static bool debugLightView = false;
    static int debugCascade = 0;

    while (engine.IsRunning()) {
        const uint64_t currentCounter = SDL_GetPerformanceCounter();
        float deltaTime = static_cast<float>((currentCounter - lastCounter) / frequency);
        lastCounter = currentCounter;

        engine.BeginFrame();

        if (audioTestEntity.IsValid() && engine.GetInput()->IsKeyPressed(SDL_SCANCODE_P)) {
            auto& audioSrc = audioTestEntity.GetComponent<Osiris::AudioSourceComponent>();
            if (engine.GetAudio()->IsSourcePlaying(audioSrc.sourceHandle)) {
                engine.GetAudio()->StopSource(audioSrc.sourceHandle);
            } else {
                engine.GetAudio()->PlaySource(audioSrc.sourceHandle);
            }
        }

        if (kPhysicsTestScene) {
            // Mouse-look only — WASD drives the character controller's velocity below,
            // not the camera's own free-fly movement.
            camera.Update(*engine.GetInput(), deltaTime, false);

            glm::vec3 flatForward = glm::normalize(glm::vec3(camera.GetFront().x, 0.0f, camera.GetFront().z));
            glm::vec3 flatRight   = glm::normalize(glm::cross(flatForward, glm::vec3(0.0f, 1.0f, 0.0f)));

            constexpr float kWalkSpeed = 4.0f;
            glm::vec3 desiredVelocity(0.0f);
            if (engine.GetInput()->IsKeyHeld(SDL_SCANCODE_W)) desiredVelocity += flatForward;
            if (engine.GetInput()->IsKeyHeld(SDL_SCANCODE_S)) desiredVelocity -= flatForward;
            if (engine.GetInput()->IsKeyHeld(SDL_SCANCODE_A)) desiredVelocity -= flatRight;
            if (engine.GetInput()->IsKeyHeld(SDL_SCANCODE_D)) desiredVelocity += flatRight;
            if (glm::length(desiredVelocity) > 0.0f) desiredVelocity = glm::normalize(desiredVelocity) * kWalkSpeed;

            bool jump = engine.GetInput()->IsKeyPressed(SDL_SCANCODE_SPACE);
            engine.GetPhysics()->SetCharacterDesiredVelocity(playerCharacter, desiredVelocity, jump);
            engine.GetPhysics()->Update(deltaTime);
            scene.SyncPhysicsTransforms(engine.GetPhysics());

            glm::vec3 feet = engine.GetPhysics()->GetCharacterPosition(playerCharacter);
            camera.SetPosition(feet + glm::vec3(0.0f, kPlayerEyeHeight, 0.0f));
        } else {
            camera.Update(*engine.GetInput(), deltaTime);
        }

        // Scripting: OnUpdate every frame, OnFixedUpdate on its own internal 60Hz accumulator —
        // run after physics/transform sync so scripts see up-to-date positions, and before the
        // spotlight gather/render below so e.g. pulse_light's intensity change lands this frame.
        engine.GetScripting()->Update(deltaTime);
        engine.GetScripting()->FixedUpdate(deltaTime);

        // Listener tracks whichever camera is actually driving the view, in either scene mode.
        engine.GetAudio()->SetListenerTransform(camera.GetPosition(), camera.GetFront(), glm::vec3(0.0f, 1.0f, 0.0f));
        scene.SyncAudioSources(engine.GetAudio());

        auto activeSpotLights = scene.GatherSpotLights(camera.GetPosition());
        engine.GetRHI()->UpdateSpotLights(activeSpotLights);

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

        // Spot light shadow passes (all slots render, even unclaimed ones, to
        // keep every shadow map's layout valid for the descriptor set).
        for (uint32_t i = 0; i < Osiris::MAX_SPOT_SHADOW_CASTERS; i++) {
            engine.GetRHI()->BeginSpotShadowPass(i);
            scene.RenderShadows(engine.GetRHI());
            engine.GetRHI()->EndSpotShadowPass(i);
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

        if (kPhysicsTestScene) {
            glm::vec3 feet = engine.GetPhysics()->GetCharacterPosition(playerCharacter);
            ImGui::Text("Character feet: %.2f %.2f %.2f", feet.x, feet.y, feet.z);
            ImGui::Text("Grounded: %s", engine.GetPhysics()->IsCharacterGrounded(playerCharacter) ? "yes" : "no");
            ImGui::Text("WASD to move, Space to jump, mouse (RMB held) to look");
            ImGui::Separator();
        }
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

        ImGui::SliderFloat("Environment Exposure", &engine.GetRHI()->GetEnvironmentExposure(),
            0.001f, 4.0f, "%.4f", ImGuiSliderFlags_Logarithmic);

        ImGui::Separator();
        int activeShadowCasters = 0;
        for (const auto& sl : activeSpotLights) {
            if (sl.shadowIndex >= 0) activeShadowCasters++;
        }
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

        sceneInspector.Draw(scene, engine.GetPhysics(), engine.GetAudio(), engine.GetScripting());

        engine.GetRHI()->RenderImGui();
        engine.EndFrame();
    }

    engine.Shutdown();
    return 0;
}