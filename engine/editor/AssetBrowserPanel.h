#ifndef OSIRIS_ASSETBROWSERPANEL_H
#define OSIRIS_ASSETBROWSERPANEL_H

#include "assets/AssetCatalog.h"

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

        std::vector<AssetEntry> m_Models;
    };
}

#endif //OSIRIS_ASSETBROWSERPANEL_H
