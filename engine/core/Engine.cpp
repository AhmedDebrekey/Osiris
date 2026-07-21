#include "Engine.h"

#include "rhi_vulkan/VulkanRHI.h"

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
    }

    void Engine::BeginFrame() {
        PollEvents();
        m_RHI->BeginFrame();
    }

    void Engine::EndFrame() const {
        m_RHI->EndFrame();
        m_RHI->Present();
    }

    void Engine::PollEvents() {
        m_Window.PollEvents();
        m_Input.Update();
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
