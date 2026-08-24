#ifndef OSIRIS_ASSETBROWSERPANEL_H
#define OSIRIS_ASSETBROWSERPANEL_H

#include "assets/AssetCatalog.h"
#include "rhi/RHITypes.h"

#include <functional>
#include <vector>

namespace Osiris {
    class Camera;
    class IRHI;
    class Scene;

    class AssetBrowserPanel {
    public:
        AssetBrowserPanel();
        void Draw(Scene& scene, const Camera& camera, IRHI* rhi);
        void DrawViewportDropTarget(Scene& scene, const Camera& camera, IRHI* rhi) const;

    private:
        static void SpawnModel(const AssetEntry& model, Scene& scene, const Camera& camera, IRHI* rhi);

        // Shared by the Models/Scripts/Scenes sections below: a filtered, icon-prefixed asset
        // list under one CollapsingHeader. dragPayloadType/onDoubleClick are both optional
        // (nullptr/empty): Scenes offers neither, since loading one is SceneFileMenu's job and
        // there's no drop target for a scene payload to land on.
        void DrawAssetSection(const char* label, const std::vector<AssetEntry>& entries,
            EditorIcon icon, const char* dragPayloadType, IRHI* rhi,
            const std::function<void(const AssetEntry&)>& onDoubleClick) const;

        std::vector<AssetEntry> m_Models;
        std::vector<AssetEntry> m_Scripts;
        std::vector<AssetEntry> m_Scenes;
        char m_FilterBuffer[128] = {};
    };
}

#endif //OSIRIS_ASSETBROWSERPANEL_H
