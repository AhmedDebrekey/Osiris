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
    class Camera;
    class Scene;

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

        // Edit mode (default) vs Play mode — client code gates behavior on this.
        bool IsPlaying() const { return m_IsPlaying; }

        // The only correct way to flip Play/Edit mode: rebuilds live physics bodies/characters
        // from the current Transform, snapshots for restore on exit, resets scripts, starts
        // autoplay audio. A bare "flip m_IsPlaying" would skip all of that and leave physics/audio/
        // scripting out of sync with the mode.
        void EnterPlayMode(Scene& scene);
        void ExitPlayMode(Scene& scene);

        void BeginFrame();
        void EndFrame() const;
        void SetEditorViewportSize(uint32_t width, uint32_t height);
        uint64_t GetEditorViewportTextureID() const;
        void UpdateCameraAspect(Camera& camera) const;
        void RenderScene(Scene& scene, Camera& camera);

        // Spot light gathering, camera buffer update, shadow cascade + spot shadow passes, the
        // forward pass, and the audio listener sync — everything a client needs per frame to
        // actually see and hear the scene, in the one order that's safe to call them in.
        // debugLightView/debugCascade are an engine-level shadow-cascade debugging aid (renders
        // the forward pass from the light's own point of view instead of the camera's) — leave at
        // the defaults for normal gameplay.
        void RenderFrame(Scene& scene, Camera& camera, bool debugLightView = false, int debugCascade = 0);
        void RenderImGui() const;

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
