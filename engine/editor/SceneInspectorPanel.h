#ifndef OSIRIS_SCENEINSPECTORPANEL_H
#define OSIRIS_SCENEINSPECTORPANEL_H

#include <entt/entt.hpp>

namespace Osiris {
    class Scene;
    class Entity;
    class IPhysics;
    class IAudio;
    class IScripting;

    // Debug-only ImGui panel: entity list + inspect/edit/add/remove components. physics/audio/
    // scripting let Remove destroy the live backend resource instead of orphaning it.
    class SceneInspectorPanel {
    public:
        void Draw(Scene& scene, IPhysics* physics, IAudio* audio, IScripting* scripting);

        entt::entity GetSelectedEntity() const { return m_SelectedEntity; }

        // Call after Scene::Clear (or any other bulk teardown) — the previously-selected handle
        // may now be stale or, worse, silently point at an unrelated recycled entity.
        void ClearSelection() { m_SelectedEntity = entt::null; }

    private:
        void DrawEntityList(Scene& scene);
        void DrawComponents(Entity entity, IPhysics* physics, IAudio* audio, IScripting* scripting);
        void DrawAddComponentButton(Entity entity);

        entt::entity m_SelectedEntity = entt::null;
    };
}

#endif //OSIRIS_SCENEINSPECTORPANEL_H
