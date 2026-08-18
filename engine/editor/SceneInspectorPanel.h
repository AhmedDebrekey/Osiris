#ifndef OSIRIS_SCENEINSPECTORPANEL_H
#define OSIRIS_SCENEINSPECTORPANEL_H

#include <entt/entt.hpp>

namespace Osiris {
    class Scene;
    class Entity;

    // Debug-only ImGui panel: lists every entity in a Scene and lets you inspect/edit
    // whichever known component types it has. Self-contained — the caller only needs to
    // keep one of these around and call Draw(scene) once per frame; no drawing logic
    // belongs at the call site.
    class SceneInspectorPanel {
    public:
        void Draw(Scene& scene);

    private:
        void DrawEntityList(Scene& scene);
        void DrawComponents(Entity entity);

        entt::entity m_SelectedEntity = entt::null;
    };
}

#endif //OSIRIS_SCENEINSPECTORPANEL_H
