#include "Engine.h"

namespace Osiris {
    bool Engine::Initialize() {

        m_Logger.Initialize();


        const WindowDesc desc{
            "Osiris",
            1280,
            720,
            true,
            false};
        m_Window.Initialize(desc);

        VulkanContextDesc vulkanContextDesc = {
            .windowHandle = m_Window.GetNativeWindow(),
            .windowWidth = m_Window.GetWidth(),
            .windowHeight = m_Window.GetHeight(),
        };

        m_VulkanContext.Initialize(vulkanContextDesc);

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

    void Engine::PollEvents() {
        m_Window.PollEvents();
        m_IsRunning = !m_Window.ShouldClose();
    }

    void Engine::Update() {
    }

    void Engine::Render() {
        m_VulkanContext.DrawFrame();
    }

    void Engine::Present() {
    }
}
