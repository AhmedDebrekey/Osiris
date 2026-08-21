#ifndef OSIRIS_EDITOR_H
#define OSIRIS_EDITOR_H

#include "SceneInspectorPanel.h"
#include "AssetBrowserPanel.h"
#include "SceneFileMenu.h"

#include <imgui.h>
#include "ImGuizmo.h"

namespace Osiris {
    class Scene;
    class Camera;
    class Engine;

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
        SceneInspectorPanel m_SceneInspector;
        AssetBrowserPanel m_AssetBrowser;
        SceneFileMenu m_SceneFileMenu;

        ImGuizmo::OPERATION m_GizmoOperation = ImGuizmo::TRANSLATE;
        bool m_DebugLightView = false;
        int m_DebugCascade = 0;
    };
}

#endif //OSIRIS_EDITOR_H
