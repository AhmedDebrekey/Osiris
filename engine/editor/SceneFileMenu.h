#ifndef OSIRIS_SCENEFILEMENU_H
#define OSIRIS_SCENEFILEMENU_H

#include "assets/AssetCatalog.h"

#include <vector>

namespace Osiris {
    class Scene;
    class IRHI;
    class IPhysics;
    class IAudio;
    class IScripting;
    class SceneInspectorPanel;

    // The editor's File menu — "Save Scene As..."/"Load Scene..." popups over whatever .json
    // files exist under assets/scenes/. Owns the Load sequence itself (Scene::Clear, then
    // SceneLoader::Load, then the CreatePhysicsBodies/CreateCharacters/CreateAudioSources/
    // CreateScriptInstances pass every scene setup needs) so main.cpp doesn't have to.
    class SceneFileMenu {
    public:
        // Call between ImGui::BeginMainMenuBar()/EndMainMenuBar().
        void Draw(Scene& scene, IRHI* rhi, IPhysics* physics, IAudio* audio, IScripting* scripting,
                  SceneInspectorPanel& sceneInspector);

    private:
        void DrawSaveAsPopup(Scene& scene);
        void DrawLoadPopup(Scene& scene, IRHI* rhi, IPhysics* physics, IAudio* audio,
                            IScripting* scripting, SceneInspectorPanel& sceneInspector);

        char m_SaveNameBuffer[128] = "level_01";
        std::vector<AssetEntry> m_SceneList;
    };
}

#endif //OSIRIS_SCENEFILEMENU_H
