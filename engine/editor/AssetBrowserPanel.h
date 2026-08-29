#ifndef OSIRIS_ASSETBROWSERPANEL_H
#define OSIRIS_ASSETBROWSERPANEL_H

#include "assets/AssetCatalog.h"
#include "rhi/RHITypes.h"

#include <string>
#include <vector>

namespace Osiris {
    class Camera;
    class IRHI;
    class Scene;

    // Content-Browser-style view of the active game's assets folder: a folder tree on the left, the
    // selected folder's contents as icon tiles on the right. Mutates the real filesystem (New
    // Folder/New Script/Delete), so BuildAssetTree gets re-run after anything that changes it,
    // including an explicit Refresh for changes made outside the app (e.g. from the OS).
    class AssetBrowserPanel {
    public:
        AssetBrowserPanel();
        void Draw(Scene& scene, const Camera& camera, IRHI* rhi);
        void DrawViewportDropTarget(Scene& scene, const Camera& camera, IRHI* rhi) const;

    private:
        enum class PendingCreate { None, Folder, Script };

        static void SpawnModel(const AssetEntry& model, Scene& scene, const Camera& camera, IRHI* rhi);
        static EditorIcon IconForFile(const std::string& fileName);
        static const char* PayloadForFile(const std::string& fileName);

        void Refresh();
        const AssetTreeNode* FindNode(const std::string& relativePath) const;

        void DrawFolderTree(const AssetTreeNode& node);
        void DrawContentGrid(const AssetTreeNode& folder, Scene& scene, const Camera& camera, IRHI* rhi);
        void DrawCreatePopup();
        void DrawDeleteConfirmPopup();
        void OpenCreatePopup(PendingCreate kind, const std::string& parentFolder);
        void OpenDeleteConfirm(const std::string& relativePath, const std::string& name, bool isDirectory);

        AssetTreeNode m_Root;
        std::string m_SelectedFolder; // relative to the active game asset root; empty = root
        char m_FilterBuffer[128] = {};

        PendingCreate m_PendingCreate = PendingCreate::None;
        std::string m_PendingCreateParent;
        char m_NewItemNameBuffer[128] = {};
        // OpenCreatePopup/OpenDeleteConfirm are called from deep inside per-tile PushID scopes
        // (to disambiguate tiles from each other), but DrawCreatePopup/DrawDeleteConfirmPopup run
        // unscoped at the top of Draw(). ImGui::OpenPopup(str_id) folds the current ID stack into
        // the popup's ID, so calling it directly from inside that PushID scope would open a
        // popup ID that the later unscoped BeginPopup(str_id) call could never match. Deferring
        // the actual OpenPopup() call to the unscoped Draw*Popup functions themselves keeps both
        // ends of each popup at the same (unscoped) ID.
        bool m_CreatePopupRequested = false;
        bool m_DeletePopupRequested = false;

        std::string m_PendingDeletePath;
        std::string m_PendingDeleteName;
        bool m_PendingDeleteIsDirectory = false;
    };
}

#endif //OSIRIS_ASSETBROWSERPANEL_H
