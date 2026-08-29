#include "Engine.h"

#include "AssetManager.h"
#include "rhi_vulkan/VulkanRHI.h"
#include "physics/jolt/JoltPhysics.h"
#include "audio/openal/OpenALAudio.h"
#include "scripting/lua/LuaScripting.h"
#include "editor/Editor.h"
#include "renderer/Camera.h"
#include "renderer/Light.h"
#include "scene/Components.h"
#include "scene/Entity.h"
#include "scene/Scene.h"
#include "ui/GameUI.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace Osiris {
    Engine::Engine() = default;
    Engine::~Engine() = default;

    bool Engine::Initialize(const WindowDesc& desc, const std::string& gameAssetRoot) {

        m_Logger.Initialize();

#ifdef OSIRIS_ENGINE_ASSET_ROOT
        std::filesystem::path engineAssetRoot = OSIRIS_ENGINE_ASSET_ROOT;
#else
        std::filesystem::path engineAssetRoot = "assets";
#endif
        std::filesystem::path activeGameAssetRoot = gameAssetRoot;
        if (char* executableDirectory = SDL_GetBasePath()) {
            const std::filesystem::path packagedAssets =
                std::filesystem::path(executableDirectory) / "assets";
            if (std::filesystem::is_regular_file(packagedAssets / "game.json")) {
                activeGameAssetRoot = packagedAssets;
                engineAssetRoot = packagedAssets;
            }
            SDL_free(executableDirectory);
        }
        AssetManager::SetAssetRoots(
            activeGameAssetRoot.lexically_normal().generic_string(),
            engineAssetRoot.lexically_normal().generic_string());

        m_Window.Initialize(desc);

        VulkanContextDesc vulkanContextDesc = {
            .windowHandle = m_Window.GetNativeWindow(),
            .windowWidth = m_Window.GetWidth(),
            .windowHeight = m_Window.GetHeight(),
            .windowTitle = m_Window.GetTitle(),
            .vsync = desc.vsync,
        };

        auto vulkanIRHI = std::make_unique<VulkanRHI>();
        vulkanIRHI->Configure(vulkanContextDesc);
        m_RHI = std::move(vulkanIRHI);
        if (!m_RHI->Init()) {
            OSIRIS_ERROR("Failed to initialize RHI!");
            return false;
        }

        m_RHI->InitImGui();

        m_Physics = std::make_unique<JoltPhysics>();
        if (!m_Physics->Init()) {
            OSIRIS_ERROR("Failed to initialize physics!");
            return false;
        }

        m_Audio = std::make_unique<OpenALAudio>();
        if (!m_Audio->Init()) {
            OSIRIS_ERROR("Failed to initialize audio!");
            return false;
        }

        m_Scripting = std::make_unique<LuaScripting>();
        if (!m_Scripting->Init(m_RHI.get(), m_Physics.get(), m_Audio.get(), &m_Input, &m_PlayCamera)) {
            OSIRIS_ERROR("Failed to initialize scripting!");
            return false;
        }

        // Constructed after AssetManager::SetAssetRoot, since the Asset Browser's catalog scan
        // (models/scripts/scenes) resolves paths through it during construction.
        m_Editor = std::make_unique<Editor>();

        m_LastCounter = SDL_GetPerformanceCounter();

        return true;
    }

    void Engine::Shutdown() {

        m_Scripting->Shutdown();
        m_Audio->Shutdown();
        m_Physics->Shutdown();
        m_RHI->ShutdownImGui();
        m_RHI->Shutdown();
        m_Window.Shutdown();

    }

    void Engine::BeginFrame() {
        const uint64_t currentCounter = SDL_GetPerformanceCounter();
        m_FrameStartCounter = currentCounter;
        m_DeltaTime = static_cast<float>(
            (currentCounter - m_LastCounter) / static_cast<double>(SDL_GetPerformanceFrequency()));
        m_LastCounter = currentCounter;
        m_FpsSampleSeconds += m_DeltaTime;
        m_FpsSampleFrames++;
        if (m_FpsSampleSeconds >= 0.25f) {
            m_DisplayedFps = static_cast<float>(m_FpsSampleFrames) / m_FpsSampleSeconds;
            m_FpsSampleSeconds = 0.0f;
            m_FpsSampleFrames = 0;
        }

        PollEvents();
        m_RHI->BeginFrame();
        GameUI::BeginFrame();
    }

    void Engine::RunFrame(Scene& scene) {
        BeginFrame();
        if (m_EditorEnabled) m_Editor->BeginFrame();

        if (m_EditorEnabled && m_Input.IsKeyPressed(SDL_SCANCODE_F5)) {
            if (m_IsPlaying) ExitPlayMode(scene); else EnterPlayMode(scene);
        }

        if (!m_IsPlaying) {
            if (m_EditorEnabled) m_Editor->Draw(scene, m_EditCamera, *this, m_DeltaTime);
        } else {
            if (m_ShowFps && m_DisplayedFps > 0.0f) {
                GameUI::DrawText(0.015f, 0.02f, UIAnchor::TopLeft,
                    "FPS: " + std::to_string(static_cast<uint32_t>(std::round(m_DisplayedFps))),
                    glm::vec4(1.0f), 18.0f);
            }

            // Scripts read last frame's synced Transform and call physics:Set*Velocity before
            // the physics step consumes it: set desired velocity, then step, then sync.
            m_Scripting->Update(m_DeltaTime);
            m_Scripting->FixedUpdate(m_DeltaTime);
            m_Physics->Update(m_DeltaTime);
            scene.SyncPhysicsTransforms(m_Physics.get());
            scene.SyncCharacterTransforms(m_Physics.get());
            scene.DispatchCollisionEvents(m_Physics.get(), m_Scripting.get());
            scene.FlushDestroyQueue(m_Physics.get(), m_Audio.get(), m_Scripting.get());

            float outDistance = 0.0f;
            auto interactableEntity = scene.FindNearestInteractableEntity(m_PlayCamera.GetPosition(), m_PlayCamera.GetFront(), outDistance);
            if (interactableEntity.IsValid()) {
                const InteractableComponent& interactable = interactableEntity.GetComponent<InteractableComponent>();
                if (outDistance < interactable.maxDistance) {
                    const std::string keyName = SDL_GetScancodeName(interactable.keyCode);
                    GameUI::DrawText(0.5f, 0.88f, UIAnchor::Center,
                        "Press " + keyName + " to interact: " + interactable.prompt, glm::vec4(1.0f), 24.0f);

                    if (m_Input.IsKeyPressed(interactable.keyCode)) {
                        scene.DispatchInteract(interactableEntity, scene.FindCameraEntity(), m_Scripting.get());
                    }
                }
            }
        }

        Camera& renderCamera = m_IsPlaying ? m_PlayCamera : m_EditCamera;
        const bool debugLightView = m_EditorEnabled && m_Editor->IsDebugLightViewEnabled();
        const int debugCascade = m_EditorEnabled ? m_Editor->GetDebugCascade() : 0;
        RenderFrame(scene, renderCamera, debugLightView, debugCascade);
        RenderImGui();
        EndFrame();
    }

    void Engine::EndFrame() const {
        m_RHI->EndFrame();
        m_RHI->Present();
        LimitFrameRate();
    }

    void Engine::LimitFrameRate() const {
        if (m_MaxFps == 0 || m_FrameStartCounter == 0) return;

        const double targetSeconds = 1.0 / static_cast<double>(m_MaxFps);
        const double elapsedSeconds = static_cast<double>(
            SDL_GetPerformanceCounter() - m_FrameStartCounter) / SDL_GetPerformanceFrequency();
        const double remainingSeconds = targetSeconds - elapsedSeconds;
        if (remainingSeconds <= 0.0) return;

        SDL_Delay(static_cast<uint32_t>(std::ceil(remainingSeconds * 1000.0)));
    }

    void Engine::SetEditorViewportSize(uint32_t width, uint32_t height) {
        if (!m_IsPlaying) m_RHI->ResizeViewport(width, height);
    }

    uint64_t Engine::GetEditorViewportTextureID() const {
        return m_RHI->GetViewportTextureID();
    }

    void Engine::UpdateCameraAspect(Camera& camera) const {
        const glm::uvec2 extent = m_RHI->GetRenderExtent(!m_IsPlaying);
        if (extent.y > 0) camera.SetAspectRatio(static_cast<float>(extent.x) / static_cast<float>(extent.y));
    }

    void Engine::RenderScene(Scene& scene, Camera& camera) {
        if (m_IsPlaying) {
            m_RHI->BeginForwardPass();
        } else {
            m_RHI->BeginViewportForwardPass();
        }

        scene.Render(m_RHI.get(), camera);
    }

    void Engine::EnterPlayMode(Scene& scene) {
        // Edit-mode Transform edits (gizmo, Inspector) never touch the live physics body/
        // character, only TransformComponent — rebuild both from the current Transform so Play
        // doesn't start from whatever pose they were created/last-simulated at.
        scene.CreatePhysicsBodies(m_Physics.get());
        scene.CreateCharacters(m_Physics.get());
        scene.CreateAudioSources(m_Audio.get());
        scene.CapturePlaySnapshot();
        scene.ResetScriptInstances(m_Scripting.get());
        scene.PlayAutoPlayAudioSources(m_Audio.get());
        m_PlayCamera.ClearShake();
        m_IsPlaying = true;
    }

    void Engine::ExitPlayMode(Scene& scene) {
        scene.RestorePlaySnapshot(m_Physics.get());
        scene.StopAllAudioSources(m_Audio.get());
        m_PlayCamera.ClearShake();
        m_IsPlaying = false;
    }

    void Engine::SyncPlayCamera(Scene& scene) {
        Entity camEntity = scene.FindCameraEntity();
        if (!camEntity.IsValid()) return;

        const auto& transform = camEntity.GetComponent<TransformComponent>();
        const float eyeHeight = camEntity.GetComponent<CameraComponent>().eyeHeight;

        const float pitch = glm::radians(transform.rotation.x);
        const float yaw = glm::radians(transform.rotation.y);
        const glm::vec3 front(-sin(yaw) * cos(pitch), sin(pitch), -cos(yaw) * cos(pitch));

        m_PlayCamera.SetPosition(transform.position + glm::vec3(0.0f, eyeHeight, 0.0f));
        m_PlayCamera.SetOrientation(front, glm::vec3(0.0f, 1.0f, 0.0f));
    }

    void Engine::RenderFrame(Scene& scene, Camera& camera, bool debugLightView, int debugCascade) {
        if (m_IsPlaying) {
            SyncPlayCamera(scene);
            m_PlayCamera.UpdateShake(m_DeltaTime);
        }

        m_Audio->SetListenerTransform(camera.GetPosition(), camera.GetFront(), glm::vec3(0.0f, 1.0f, 0.0f));
        scene.SyncAudioSources(m_Audio.get());

        // Edit mode already updated the aspect ratio once before the editor's gizmo needed it
        // this frame; doing it again here would just repeat the same work.
        if (m_IsPlaying) UpdateCameraAspect(camera);

        auto activeSpotLights = scene.GatherSpotLights(camera.GetPosition());
        m_RHI->UpdateSpotLights(activeSpotLights);
        const glm::vec3 renderPosition = camera.GetRenderPosition();
        const glm::vec3 renderFront = camera.GetRenderFront();
        m_RHI->UpdateCamera(camera.GetViewMatrix(), camera.GetProjectionMatrix(),
            glm::vec4(renderPosition, 0.0f), renderFront);

        if (debugLightView) {
            const glm::mat4 lightView = m_RHI->GetLightViewMatrix(debugCascade);
            const glm::mat4 lightProj = m_RHI->GetLightProjMatrix(debugCascade);
            m_RHI->SetCameraBuffer(lightView, lightProj, glm::vec4(renderPosition, 0.0f));
        }

        for (uint32_t i = 0; i < 3; i++) {
            const std::string timingName = "Cascade " + std::to_string(i);
            m_RHI->BeginGPUTimestamp(timingName);
            m_RHI->BeginShadowPass(i);
            scene.RenderShadows(m_RHI.get());
            m_RHI->EndShadowPass(i);
            m_RHI->EndGPUTimestamp(timingName);
        }

        // Every slot still begins and ends a pass so its map reaches the layout expected by the
        // descriptor set. Unclaimed slots only clear; redrawing all scene geometry into a shadow
        // map no light can sample wastes substantial fixed GPU work.
        for (uint32_t i = 0; i < MAX_SPOT_SHADOW_CASTERS; i++) {
            const std::string timingName = "Spot Shadow " + std::to_string(i);
            const bool slotClaimed = std::ranges::any_of(activeSpotLights,
                [i](const SpotLightRenderData& light) {
                    return light.shadowIndex == static_cast<int>(i);
                });
            m_RHI->BeginGPUTimestamp(timingName);
            m_RHI->BeginSpotShadowPass(i);
            if (slotClaimed) scene.RenderShadows(m_RHI.get());
            m_RHI->EndSpotShadowPass(i);
            m_RHI->EndGPUTimestamp(timingName);
        }

        m_RHI->BeginGPUTimestamp("Forward Pass");
        RenderScene(scene, camera);
        m_RHI->EndGPUTimestamp("Forward Pass");
    }

    void Engine::RenderImGui() const {
        if (m_IsPlaying) GameUI::DrawQueuedOverlays();
        m_RHI->RenderImGui(!m_IsPlaying);
    }

    void Engine::PollEvents() {
        m_Window.PollEvents();
        m_Input.Update();
        m_RHI->BeginImGuiFrame();
        m_IsRunning = !m_Window.ShouldClose();
    }
}
