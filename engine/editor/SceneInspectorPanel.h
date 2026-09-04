#ifndef OSIRIS_SCENEINSPECTORPANEL_H
#define OSIRIS_SCENEINSPECTORPANEL_H

#include <entt/entt.hpp>

namespace Osiris {
    class Scene;
    class Entity;
    class IPhysics;
    class IAudio;
    class IScripting;

    // Debug-only ImGui panels: DrawHierarchy is the entity tree ("Scene Hierarchy"), DrawProperties
    // is the selected entity's component editor ("Details"), split like Unreal's Outliner/Details
    // pair so either can be docked independently. Both read/write the same selection state, which
    // is why this stays one class rather than two. physics/audio/scripting let Remove destroy the
    // live backend resource instead of orphaning it.
    class SceneInspectorPanel {
    public:
        void DrawHierarchy(Scene& scene, IPhysics* physics, IAudio* audio, IScripting* scripting);
        void DrawProperties(Scene& scene, IPhysics* physics, IAudio* audio, IScripting* scripting);

        entt::entity GetSelectedEntity() const { return m_SelectedEntity; }
        void SetSelectedEntity(entt::entity entity) {
            if (m_SelectedEntity != entity) {
                m_ColliderEditEntity = entt::null;
                m_GroundFailed = false;
            }
            m_SelectedEntity = entity;
        }
        bool IsColliderEditing() const {
            return m_SelectedEntity != entt::null && m_ColliderEditEntity == m_SelectedEntity;
        }

        // Call after Scene::Clear (or any other bulk teardown) — the previously-selected handle
        // may now be stale or, worse, silently point at an unrelated recycled entity.
        void ClearSelection() {
            m_SelectedEntity = entt::null;
            m_ColliderEditEntity = entt::null;
            m_GroundFailed = false;
        }

    private:
        void DrawEntityNode(Scene& scene, Entity entity, entt::entity& pendingDelete, IAudio* audio, IScripting* scripting);
        void DrawComponents(Entity entity, IPhysics* physics, IAudio* audio, IScripting* scripting);
        void DrawAddComponentButton(Entity entity, IPhysics* physics);

        // Shared by DrawEntityNode's per-row drop target and DrawProperties' whole-panel one:
        // must be called from inside an already-open BeginDragDropTarget()/EndDragDropTarget() pair.
        static void HandleAssetDrop(Entity entity, IAudio* audio, IScripting* scripting);

        entt::entity m_SelectedEntity = entt::null;
        entt::entity m_ColliderEditEntity = entt::null;
        bool m_GroundFailed = false;
    };
}

#endif //OSIRIS_SCENEINSPECTORPANEL_H
