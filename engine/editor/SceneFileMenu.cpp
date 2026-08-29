#include "SceneFileMenu.h"

#include "assets/SceneLoader.h"
#include "core/AssetManager.h"
#include "editor/GameExporter.h"
#include "editor/SceneInspectorPanel.h"
#include "scene/Scene.h"

#include <imgui.h>
#include <cstring>

namespace Osiris {
    void SceneFileMenu::Draw(Scene& scene, IRHI* rhi, IPhysics* physics, IAudio* audio, IScripting* scripting,
                              SceneInspectorPanel& sceneInspector, uint32_t maxFps, bool showFps) {
        bool openSaveAs = false;
        bool openLoad = false;
        bool openExport = false;

        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Save Scene As...")) openSaveAs = true;
            if (ImGui::MenuItem("Load Scene...")) openLoad = true;
            ImGui::Separator();
            if (ImGui::MenuItem("Export Game...")) openExport = true;
            ImGui::EndMenu();
        }

        // OpenPopup deliberately fires out here, not inside BeginMenu/EndMenu above — a submenu
        // is its own ImGui window internally, and popup IDs are computed relative to the current
        // window, so calling OpenPopup("Save Scene As") inside the menu registers a different ID
        // than BeginPopup("Save Scene As") below (called from this outer scope) looks for. The
        // popup would silently never open.
        if (openSaveAs) {
            m_SceneList = AssetCatalog::ListScenes();
            ImGui::OpenPopup("Save Scene As");
        }
        if (openLoad) {
            m_SceneList = AssetCatalog::ListScenes();
            ImGui::OpenPopup("Load Scene");
        }
        if (openExport) {
            m_ExportStatus.clear();
            ImGui::OpenPopup("Export Game");
        }

        DrawSaveAsPopup(scene);
        DrawLoadPopup(scene, rhi, physics, audio, scripting, sceneInspector);
        DrawExportPopup(scene, maxFps, showFps);
    }

    void SceneFileMenu::DrawSaveAsPopup(Scene& scene) {
        if (!ImGui::BeginPopup("Save Scene As")) return;

        ImGui::TextUnformatted("Save to the active game's assets/scenes/");
        ImGui::InputText("Name", m_SaveNameBuffer, sizeof(m_SaveNameBuffer));
        ImGui::TextDisabled(".json added automatically");

        if (!m_SceneList.empty()) {
            ImGui::Separator();
            ImGui::TextDisabled("Existing scenes (click to fill in, still need Save):");
            for (const AssetEntry& sceneAsset : m_SceneList) {
                ImGui::PushID(sceneAsset.relativePath.c_str());
                if (ImGui::Selectable(sceneAsset.name.c_str())) {
                    strncpy_s(m_SaveNameBuffer, sceneAsset.name.c_str(), sizeof(m_SaveNameBuffer) - 1);
                }
                ImGui::PopID();
            }
        }

        ImGui::Separator();
        const bool validName = m_SaveNameBuffer[0] != '\0';
        if (!validName) ImGui::BeginDisabled();
        if (ImGui::Button("Save")) {
            const std::string relativePath = std::string("scenes/") + m_SaveNameBuffer + ".json";
            SceneLoader::Save(AssetManager::GetPath(relativePath), scene);
            m_SceneList = AssetCatalog::ListScenes();
            ImGui::CloseCurrentPopup();
        }
        if (!validName) ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }

    void SceneFileMenu::DrawLoadPopup(Scene& scene, IRHI* rhi, IPhysics* physics, IAudio* audio,
                                       IScripting* scripting, SceneInspectorPanel& sceneInspector) {
        if (!ImGui::BeginPopup("Load Scene")) return;

        if (m_SceneList.empty()) {
            ImGui::TextDisabled("No .json scenes found under the active game's assets/scenes/.");
        }

        for (const AssetEntry& sceneAsset : m_SceneList) {
            ImGui::PushID(sceneAsset.relativePath.c_str());
            if (ImGui::Selectable(sceneAsset.name.c_str())) {
                scene.Clear(physics, audio, scripting);
                sceneInspector.ClearSelection();

                SceneLoader::Load(AssetManager::GetPath(sceneAsset.relativePath), scene, rhi, audio);
                scene.CreatePhysicsBodies(physics);
                scene.CreateCharacters(physics);
                scene.CreateAudioSources(audio);
                scene.StopAllAudioSources(audio); // Edit mode is the starting state — see the same call at initial scene setup.
                scene.CreateScriptInstances(scripting);

                strncpy_s(m_SaveNameBuffer, sceneAsset.name.c_str(), sizeof(m_SaveNameBuffer) - 1);
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
        }

        ImGui::EndPopup();
    }

    void SceneFileMenu::DrawExportPopup(Scene& scene, uint32_t maxFps, bool showFps) {
        if (!ImGui::BeginPopup("Export Game")) return;

        ImGui::TextWrapped("Creates a portable Windows folder and ZIP containing the current scene, assets, executable, and runtime DLLs.");
        ImGui::InputText("Game name", m_ExportNameBuffer, sizeof(m_ExportNameBuffer));
        ImGui::TextDisabled("Output: the active game's exports/ folder");

#ifndef NDEBUG
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.25f, 1.0f),
            "Export Game is available when running the Release configuration.");
#endif

        if (!m_ExportStatus.empty()) {
            ImGui::Separator();
            const ImVec4 color = m_LastExportSucceeded
                ? ImVec4(0.45f, 0.9f, 0.45f, 1.0f)
                : ImVec4(1.0f, 0.45f, 0.4f, 1.0f);
            ImGui::TextColored(color, "%s", m_ExportStatus.c_str());
        }

        ImGui::Separator();
        const bool validName = m_ExportNameBuffer[0] != '\0';
#ifndef NDEBUG
        ImGui::BeginDisabled();
#else
        if (!validName) ImGui::BeginDisabled();
#endif
        if (ImGui::Button("Export")) {
            const GameExportResult result = GameExporter::Export(
                scene, m_ExportNameBuffer, maxFps, showFps);
            m_LastExportSucceeded = result.succeeded;
            m_ExportStatus = result.message;
        }
#ifndef NDEBUG
        ImGui::EndDisabled();
#else
        if (!validName) ImGui::EndDisabled();
#endif
        ImGui::SameLine();
        if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }
}
