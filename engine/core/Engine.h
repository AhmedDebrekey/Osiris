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
#include "renderer/Camera.h"
namespace Osiris {
    class Scene;
    class Editor;

    class Engine {
    public:
        // Both defined in Engine.cpp, not defaulted here: std::unique_ptr<Editor>'s destructor
        // (needed by both, the constructor for exception-unwind cleanup) requires Editor's
        // complete type, which Engine.h only forward-declares.
        Engine();
        ~Engine();


        Engine(const Engine &)=delete;
        Engine &operator=(const Engine &)=delete;

        bool Initialize(const WindowDesc& desc = {}, const std::string& gameAssetRoot = "assets");
        void Shutdown();

        // Drives one full frame end to end: delta time, input, Play/Edit toggle (F5), the
        // editor when not playing, scripting/physics/sync/collision when playing, then render +
        // present. A client's whole loop is just `while (engine.IsRunning()) engine.RunFrame(scene);`
        // BeginFrame/RenderFrame/EnterPlayMode/etc. below stay available individually for a
        // client that wants a custom loop instead (a different toggle key, a different camera
        // provider), but RunFrame is the one to reach for by default.
        void RunFrame(Scene& scene);

        float GetDeltaTime() const { return m_DeltaTime; }

        IRHI* GetRHI() const { return m_RHI.get(); }
        IPhysics* GetPhysics() const { return m_Physics.get(); }
        IAudio* GetAudio() const { return m_Audio.get(); }
        IScripting* GetScripting() const { return m_Scripting.get(); }

        Input* GetInput() { return &m_Input; }

        bool IsRunning() const { return m_IsRunning; }

        // Edit mode (default) vs Play mode — client code gates behavior on this.
        bool IsPlaying() const { return m_IsPlaying; }
        bool IsEditorEnabled() const { return m_EditorEnabled; }
        void SetEditorEnabled(bool enabled) { m_EditorEnabled = enabled; }
        uint32_t GetMaxFps() const { return m_MaxFps; }
        void SetMaxFps(uint32_t maxFps) { m_MaxFps = maxFps; }
        bool IsFpsVisible() const { return m_ShowFps; }
        void SetFpsVisible(bool visible) { m_ShowFps = visible; }

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

        // Kept in sync with Scene::FindCameraEntity() automatically (see RenderFrame) whenever
        // Play mode is running, no per-game code required for the sync itself: a client only
        // needs this if it actually wants to render with the entity-driven camera; passing your
        // own Camera to RenderFrame instead (as the CTF testbed does) is equally valid and doesn't
        // fight this, since the sync runs regardless of which camera ends up rendering.
        Camera& GetPlayCamera() { return m_PlayCamera; }

    private:
        void PollEvents();
        void LimitFrameRate() const;
        // Follows the scene's primary CameraComponent entity, if any: position = entity position
        // + eyeHeight, orientation from TransformComponent.rotation (x=pitch, y=yaw, same
        // convention tank_controller.lua already uses for its own forward-vector math). A no-op
        // when the scene has no CameraComponent entity, so this is always safe to call.
        void SyncPlayCamera(Scene& scene);
        bool m_IsRunning = true;
        bool m_IsPlaying = false;
        bool m_EditorEnabled = true;
        bool m_ShowFps = true;
        uint32_t m_MaxFps = 120;

        Window m_Window;

        Input m_Input;

        Log m_Logger;

        std::unique_ptr<IRHI> m_RHI;
        std::unique_ptr<IPhysics> m_Physics;
        std::unique_ptr<IAudio> m_Audio;
        std::unique_ptr<IScripting> m_Scripting;
        std::unique_ptr<Editor> m_Editor;

        Camera m_PlayCamera{glm::vec3(0.0f, 1.5f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f)};
        Camera m_EditCamera{glm::vec3(0.0f, 1.5f, 4.0f), glm::vec3(0.0f, 0.0f, -1.0f)};

        uint64_t m_LastCounter = 0;
        uint64_t m_FrameStartCounter = 0;
        float m_DeltaTime = 0.0f;
        float m_FpsSampleSeconds = 0.0f;
        uint32_t m_FpsSampleFrames = 0;
        float m_DisplayedFps = 0.0f;
    };
}

#endif
