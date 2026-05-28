#include "Engine.h"

namespace Osiris {
    bool Engine::Initialize() {

        m_Logger.Initialize();

        m_VulkanContext.Initialize();

        const WindowDesc desc{
            "Osiris",
            1280,
            720,
            true,
            false};
        m_Window.Initialize(desc);
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
    }

    void Engine::Present() {
    }
}
