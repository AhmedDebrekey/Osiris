#ifndef OSIRIS_SCENEFILEMENU_H
#define OSIRIS_SCENEFILEMENU_H

#include "assets/AssetCatalog.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Osiris {
    class Scene;
    class IRHI;
    class IPhysics;
    class IAudio;
    class IScripting;
    class SceneInspectorPanel;

    // The editor's File menu: "Save Scene As..."/"Load Scene..." popups over whatever .json
    // files exist under the active game's assets/scenes/. Owns the Load sequence itself (Scene::Clear, then
    // SceneLoader::Load, then the CreatePhysicsBodies/CreateCharacters/CreateAudioSources/
    // CreateScriptInstances pass every scene setup needs) so main.cpp doesn't have to.
    class SceneFileMenu {
    public:
        // Call between ImGui::BeginMainMenuBar()/EndMainMenuBar().
        void Draw(Scene& scene, IRHI* rhi, IPhysics* physics, IAudio* audio, IScripting* scripting,
                  SceneInspectorPanel& sceneInspector, uint32_t maxFps, bool showFps);

    private:
        void DrawSaveAsPopup(Scene& scene);
        void DrawLoadPopup(Scene& scene, IRHI* rhi, IPhysics* physics, IAudio* audio,
                            IScripting* scripting, SceneInspectorPanel& sceneInspector);
        void DrawExportPopup(Scene& scene, uint32_t maxFps, bool showFps);

        char m_SaveNameBuffer[128] = "level_01";
        char m_ExportNameBuffer[128] = "OsirisGame";
        std::vector<AssetEntry> m_SceneList;
        std::string m_ExportStatus;
        bool m_LastExportSucceeded = false;
    };
}

#endif //OSIRIS_SCENEFILEMENU_H
