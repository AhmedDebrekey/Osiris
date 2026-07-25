//
// Created by ahtal on 25/07/2026.
//

#include "AssetManager.h"

namespace Osiris {
    std::string AssetManager::s_AssetRoot = "";
    void AssetManager::SetAssetRoot(const std::string& root) {
        s_AssetRoot = root;
    }

    std::string AssetManager::GetPath(const std::string& relativePath) {
        return s_AssetRoot + "/" + relativePath;
    }
} // Osiris