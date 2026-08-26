//
// Created by ahtal on 25/07/2026.
//

#include "AssetManager.h"

#include <filesystem>

namespace Osiris {
    std::string AssetManager::s_AssetRoot = "";
    void AssetManager::SetAssetRoot(const std::string& root) {
        s_AssetRoot = root;
    }

    std::string AssetManager::GetPath(const std::string& relativePath) {
        const std::filesystem::path input(relativePath);
        if (input.is_absolute()) return input.lexically_normal().generic_string();
        return (std::filesystem::path(s_AssetRoot) / GetRelativePath(relativePath))
            .lexically_normal().generic_string();
    }

    std::string AssetManager::GetRelativePath(const std::string& path) {
        namespace fs = std::filesystem;

        if (path.empty()) return {};

        const fs::path input(path);
        const fs::path root = input.is_absolute()
            ? fs::absolute(s_AssetRoot).lexically_normal()
            : fs::path(s_AssetRoot).lexically_normal();
        const fs::path normalized = input.lexically_normal();
        const fs::path relative = normalized.lexically_relative(root);

        if (relative == ".") return {};
        if (!relative.empty() && *relative.begin() != "..") return relative.generic_string();
        return normalized.generic_string();
    }
} // Osiris
