#include "Engine.h"

#include "AssetManager.h"
#include "rhi_vulkan/VulkanRHI.h"
#include "physics/jolt/JoltPhysics.h"
#include "audio/openal/OpenALAudio.h"
#include "scripting/lua/LuaScripting.h"
#include "renderer/Camera.h"
#include "renderer/Light.h"
#include "scene/Scene.h"

namespace Osiris {
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

        return true;
    }

    void Engine::Run()
    {
        while (m_IsRunning)
        {
            PollEvents();
            Update();
            Render();
            Present();
        }
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
        PollEvents();
        m_RHI->BeginFrame();
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

    void Engine::RenderFrame(Scene& scene, Camera& camera, bool debugLightView, int debugCascade) {
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

    void Engine::Update() {
    }

    void Engine::Render() {
        m_RHI->BeginFrame();
        m_RHI->EndFrame();
        m_RHI->Present();
    }

    void Engine::Present() {
    }
}
