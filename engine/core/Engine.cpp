#include "Engine.h"

#include "AssetManager.h"
#include "rhi_vulkan/VulkanRHI.h"
#include "physics/jolt/JoltPhysics.h"
#include "audio/openal/OpenALAudio.h"
#include "scripting/lua/LuaScripting.h"
#include "renderer/Camera.h"
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
