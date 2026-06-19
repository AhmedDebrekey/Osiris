#ifndef OSIRIS_ENGINE_H
#define OSIRIS_ENGINE_H

#include "platform/Window.h"
#include "EngineConfig.h"
#include "Log.h"
#include "rhi/RHI.h"
namespace Osiris {
    class Engine {
    public:
        Engine() = default;
        ~Engine() = default;


        Engine(const Engine &)=delete;
        Engine &operator=(const Engine &)=delete;

        bool Initialize();
        void Run();
        void Shutdown();

        IRHI* GetRHI() const { return m_RHI.get(); }

        bool IsRunning() const { return m_IsRunning; }

        void BeginFrame();
        void EndFrame() const;

    private:
        void PollEvents();
        void Update();
        void Render();
        void Present();
        bool m_IsRunning = true;

        Window m_Window;

        Log m_Logger;

        std::unique_ptr<IRHI> m_RHI;
    };
}

#endif