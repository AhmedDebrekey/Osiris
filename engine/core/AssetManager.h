//
// Created by Debreky on 25/07/2026.
//

#ifndef OSIRIS_ASSETMANAGER_H
#define OSIRIS_ASSETMANAGER_H
#include <string>

namespace Osiris {
    class AssetManager {
    public:
        static void SetAssetRoot(const std::string& root);
        static std::string GetPath(const std::string& relativePath);
        static std::string GetRelativePath(const std::string& path);

    private:
        static std::string s_AssetRoot;
    };
} // Osiris

#endif //OSIRIS_ASSETMANAGER_H
