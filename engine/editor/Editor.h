#ifndef OSIRIS_EDITOR_H
#define OSIRIS_EDITOR_H

#include "SceneInspectorPanel.h"
#include "AssetBrowserPanel.h"
#include "RenderDebugPanel.h"
#include "SceneFileMenu.h"

#include <imgui.h>
#include "ImGuizmo.h"
#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>

namespace Osiris {
    class Scene;
    class Camera;
    class Engine;
    class Entity;
    class IPhysics;

    // Owns every ImGui/gizmo/editor-tooling concern (docked viewport, transform gizmos, asset
    // browser, scene inspector, debug panels), so games/testbed/main.cpp stays a plain game
    // client with no editor-specific code in it. A shipped game can skip constructing this
    // entirely.
    class Editor {
    public:
        // Call once per frame, unconditionally, even in Play mode: resets ImGuizmo's own
        // per-frame interaction state, so it doesn't carry stale drag/hover state into the next
        // time Edit mode draws a gizmo.
        void BeginFrame();

        // Only call while !engine.IsPlaying(): builds the whole editor UI for this frame.
        void Draw(Scene& scene, Camera& camera, Engine& engine, float deltaTime);

        bool IsDebugLightViewEnabled() const { return m_DebugLightView; }
        int GetDebugCascade() const { return m_DebugCascade; }

    private:
        bool DrawBoxColliderOverlay(Scene& scene, Entity entity, const glm::mat4& entityWorld,
            const Camera& camera, IPhysics* physics, const ImVec2& viewportMin, const ImVec2& viewportMax);

        SceneInspectorPanel m_SceneInspector;
        AssetBrowserPanel m_AssetBrowser;
        RenderDebugPanel m_RenderDebugPanel;
        SceneFileMenu m_SceneFileMenu;

        ImGuizmo::OPERATION m_GizmoOperation = ImGuizmo::TRANSLATE;
        entt::entity m_ColliderFaceDragEntity = entt::null;
        int m_ColliderFaceDragAxis = -1;
        float m_ColliderFaceDragSign = 1.0f;
        ImVec2 m_ColliderFaceDragMouseStart{};
        ImVec2 m_ColliderFaceDragAxisScreen{};
        float m_ColliderFaceDragPixelsPerUnit = 1.0f;
        glm::vec3 m_ColliderFaceDragStartCenter{0.0f};
        glm::vec3 m_ColliderFaceDragStartHalfExtents{0.5f};
        bool m_ColliderCenterWasUsing = false;
        bool m_ColliderCenterChanged = false;
        bool m_DebugLightView = false;
        int m_DebugCascade = 0;
    };
}

#endif //OSIRIS_EDITOR_H
