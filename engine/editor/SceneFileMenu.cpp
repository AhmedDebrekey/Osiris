#include "SceneFileMenu.h"

#include "assets/SceneLoader.h"
#include "core/AssetManager.h"
#include "editor/SceneInspectorPanel.h"
#include "scene/Scene.h"

#include <imgui.h>
#include <cstring>

namespace Osiris {
    void SceneFileMenu::Draw(Scene& scene, IRHI* rhi, IPhysics* physics, IAudio* audio, IScripting* scripting,
                              SceneInspectorPanel& sceneInspector) {
        bool openSaveAs = false;
        bool openLoad = false;

        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Save Scene As...")) openSaveAs = true;
            if (ImGui::MenuItem("Load Scene...")) openLoad = true;
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

        DrawSaveAsPopup(scene);
        DrawLoadPopup(scene, rhi, physics, audio, scripting, sceneInspector);
    }

    void SceneFileMenu::DrawSaveAsPopup(Scene& scene) {
        if (!ImGui::BeginPopup("Save Scene As")) return;

        ImGui::TextUnformatted("Save to assets/scenes/");
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
            ImGui::TextDisabled("No .json scenes found under assets/scenes/.");
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
}
