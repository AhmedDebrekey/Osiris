#ifndef OSIRIS_SCENEINSPECTORPANEL_H
#define OSIRIS_SCENEINSPECTORPANEL_H

#include <entt/entt.hpp>

namespace Osiris {
    class Scene;
    class Entity;
    class IPhysics;
    class IAudio;
    class IScripting;

    // Debug-only ImGui panel: lists every entity in a Scene and lets you inspect/edit/add/remove
    // whichever known component types it has. Self-contained — the caller only needs to keep one
    // of these around and call Draw() once per frame; no drawing logic belongs at the call site.
    // physics/audio/scripting are needed only so Remove can destroy the live backend resource a
    // RigidBody/AudioSource/Script component owns (Jolt body/OpenAL source/Lua instance) instead
    // of orphaning it — pass whatever Engine::GetPhysics()/GetAudio()/GetScripting() return.
    class SceneInspectorPanel {
    public:
        void Draw(Scene& scene, IPhysics* physics, IAudio* audio, IScripting* scripting);

    private:
        void DrawEntityList(Scene& scene);
        void DrawComponents(Entity entity, IPhysics* physics, IAudio* audio, IScripting* scripting);
        void DrawAddComponentButton(Entity entity);

        entt::entity m_SelectedEntity = entt::null;
    };
}

#endif //OSIRIS_SCENEINSPECTORPANEL_H
