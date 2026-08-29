//
// Created by Debreky on 25/07/2026.
//

#ifndef OSIRIS_ASSETMANAGER_H
#define OSIRIS_ASSETMANAGER_H
#include <string>

namespace Osiris {
    class AssetManager {
    public:
        static void SetAssetRoots(const std::string& gameRoot, const std::string& engineRoot);
        static std::string GetPath(const std::string& relativePath);
        static std::string GetEnginePath(const std::string& relativePath);
        static std::string GetRelativePath(const std::string& path);
        static const std::string& GetAssetRoot() { return s_AssetRoot; }
        static const std::string& GetEngineAssetRoot() { return s_EngineAssetRoot; }

    private:
        static std::string s_AssetRoot;
        static std::string s_EngineAssetRoot;
    };
} // Osiris

#endif //OSIRIS_ASSETMANAGER_H
