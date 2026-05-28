#ifndef OSIRIS_ENGINE_H
#define OSIRIS_ENGINE_H

#include "platform/Window.h"
#include "EngineConfig.h"

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

    private:
        void PollEvents();
        void Update();
        void Render();
        void Present();
        bool m_IsRunning = true;

        Window m_Window;

    };
}

#endif