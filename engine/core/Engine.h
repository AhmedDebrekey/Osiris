#ifndef OSIRIS_ENGINE_H
#define OSIRIS_ENGINE_H

#include "platform/Window.h"
#include "EngineConfig.h"
#include "Log.h"
#include "rhi/RHI.h"
#include "platform/Input.h"
#include "physics/IPhysics.h"
#include "audio/IAudio.h"
#include "scripting/IScripting.h"
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
        IPhysics* GetPhysics() const { return m_Physics.get(); }
        IAudio* GetAudio() const { return m_Audio.get(); }
        IScripting* GetScripting() const { return m_Scripting.get(); }

        Input* GetInput() { return &m_Input; }

        bool IsRunning() const { return m_IsRunning; }

        // Edit mode (default) vs Play mode — main.cpp gates behavior on this.
        bool IsPlaying() const { return m_IsPlaying; }
        void TogglePlaying() { m_IsPlaying = !m_IsPlaying; }

        void BeginFrame();
        void EndFrame() const;

    private:
        void PollEvents();
        void Update();
        void Render();
        void Present();
        bool m_IsRunning = true;
        bool m_IsPlaying = false;

        Window m_Window;

        Input m_Input;

        Log m_Logger;

        std::unique_ptr<IRHI> m_RHI;
        std::unique_ptr<IPhysics> m_Physics;
        std::unique_ptr<IAudio> m_Audio;
        std::unique_ptr<IScripting> m_Scripting;
    };
}

#endif