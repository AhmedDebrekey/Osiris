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

namespace Osiris {
    Engine::Engine() = default;
    Engine::~Engine() = default;

    bool Engine::Initialize() {

        m_Logger.Initialize();

        const WindowDesc desc = {
            .title = "Osiris",
            .width = 1280,
            .height = 720,
            .vsync = true,
            .fullscreen = false,
        };
        m_Window.Initialize(desc);

        VulkanContextDesc vulkanContextDesc = {
            .windowHandle = m_Window.GetNativeWindow(),
            .windowWidth = m_Window.GetWidth(),
            .windowHeight = m_Window.GetHeight(),
            .windowTitle = m_Window.GetTitle(),
        };

        auto vulkanIRHI = std::make_unique<VulkanRHI>();
        vulkanIRHI->Configure(vulkanContextDesc);
        m_RHI = std::move(vulkanIRHI);
        if (!m_RHI->Init()) {
            OSIRIS_ERROR("Failed to initialize RHI!");
            return false;
        }

        AssetManager::SetAssetRoot("assets");

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
        if (!m_Scripting->Init(m_RHI.get(), m_Physics.get(), m_Audio.get(), &m_Input)) {
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
        m_DeltaTime = static_cast<float>(
            (currentCounter - m_LastCounter) / static_cast<double>(SDL_GetPerformanceFrequency()));
        m_LastCounter = currentCounter;

        PollEvents();
        m_RHI->BeginFrame();
    }

    void Engine::RunFrame(Scene& scene) {
        BeginFrame();
        m_Editor->BeginFrame();

        if (m_Input.IsKeyPressed(SDL_SCANCODE_F5)) {
            if (m_IsPlaying) ExitPlayMode(scene); else EnterPlayMode(scene);
        }

        if (!m_IsPlaying) {
            m_Editor->Draw(scene, m_EditCamera, *this, m_DeltaTime);
        } else {
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
        RenderFrame(scene, renderCamera, m_Editor->IsDebugLightViewEnabled(), m_Editor->GetDebugCascade());
        RenderImGui();
        EndFrame();
    }

    void Engine::EndFrame() const {
        m_RHI->EndFrame();
        m_RHI->Present();
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
        m_IsPlaying = true;
    }

    void Engine::ExitPlayMode(Scene& scene) {
        scene.RestorePlaySnapshot(m_Physics.get());
        scene.StopAllAudioSources(m_Audio.get());
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
        if (m_IsPlaying) SyncPlayCamera(scene);

        m_Audio->SetListenerTransform(camera.GetPosition(), camera.GetFront(), glm::vec3(0.0f, 1.0f, 0.0f));
        scene.SyncAudioSources(m_Audio.get());

        // Edit mode already updated the aspect ratio once before the editor's gizmo needed it
        // this frame; doing it again here would just repeat the same work.
        if (m_IsPlaying) UpdateCameraAspect(camera);

        auto activeSpotLights = scene.GatherSpotLights(camera.GetPosition());
        m_RHI->UpdateSpotLights(activeSpotLights);
        m_RHI->UpdateCamera(camera.GetViewMatrix(), camera.GetProjectionMatrix(),
            glm::vec4(camera.GetPosition(), 0.0f), camera.GetFront());

        if (debugLightView) {
            const glm::mat4 lightView = m_RHI->GetLightViewMatrix(debugCascade);
            const glm::mat4 lightProj = m_RHI->GetLightProjMatrix(debugCascade);
            m_RHI->SetCameraBuffer(lightView, lightProj, glm::vec4(camera.GetPosition(), 0.0f));
        }

        for (uint32_t i = 0; i < 3; i++) {
            const std::string timingName = "Cascade " + std::to_string(i);
            m_RHI->BeginGPUTimestamp(timingName);
            m_RHI->BeginShadowPass(i);
            scene.RenderShadows(m_RHI.get());
            m_RHI->EndShadowPass(i);
            m_RHI->EndGPUTimestamp(timingName);
        }

        // All spot shadow slots render every frame, even unclaimed ones, to keep every shadow
        // map's layout valid for the descriptor set.
        for (uint32_t i = 0; i < MAX_SPOT_SHADOW_CASTERS; i++) {
            const std::string timingName = "Spot Shadow " + std::to_string(i);
            m_RHI->BeginGPUTimestamp(timingName);
            m_RHI->BeginSpotShadowPass(i);
            scene.RenderShadows(m_RHI.get());
            m_RHI->EndSpotShadowPass(i);
            m_RHI->EndGPUTimestamp(timingName);
        }

        m_RHI->BeginGPUTimestamp("Forward Pass");
        RenderScene(scene, camera);
        m_RHI->EndGPUTimestamp("Forward Pass");
    }

    void Engine::RenderImGui() const {
        m_RHI->RenderImGui(!m_IsPlaying);
    }

    void Engine::PollEvents() {
        m_Window.PollEvents();
        m_Input.Update();
        m_RHI->BeginImGuiFrame();
        m_IsRunning = !m_Window.ShouldClose();
    }
}
