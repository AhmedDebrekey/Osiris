#ifndef OSIRIS_ASSETCATALOG_H
#define OSIRIS_ASSETCATALOG_H

#include <string>
#include <vector>

namespace Osiris {
    struct AssetEntry {
        std::string name;
        std::string relativePath;
    };

    // One level of the whole assets/ directory tree (AssetCatalog::BuildAssetTree). name/
    // relativePath are just this node's own segment/path, not the parent's. relativePath is
    // AssetManager-relative, matching AssetEntry's convention. children is only populated for
    // directories, folders sorted before files, both alphabetical.
    struct AssetTreeNode {
        std::string name;
        std::string relativePath;
        bool isDirectory = false;
        std::vector<AssetTreeNode> children;
    };

    class AssetCatalog {
    public:
        static std::vector<AssetEntry> ListScenes();

        // Recursively scans the whole assets/ folder into a tree of folders and files, for the
        // Asset Browser's Content-Browser-style view. Rebuild (don't try to patch) after any
        // change made outside the running process's own file operations, since there's no
        // filesystem watcher backing this.
        static AssetTreeNode BuildAssetTree();
    };
}

#endif //OSIRIS_ASSETCATALOG_H
