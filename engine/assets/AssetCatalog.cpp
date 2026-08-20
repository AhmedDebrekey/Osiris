#include "AssetCatalog.h"

#include "core/AssetManager.h"

#include <algorithm>
#include <filesystem>

namespace Osiris {
    std::vector<ModelAsset> AssetCatalog::ListModels() {
        namespace fs = std::filesystem;

        const fs::path modelRoot = AssetManager::GetPath("models");
        std::vector<ModelAsset> models;

        std::error_code error;
        if (!fs::is_directory(modelRoot, error)) return models;

        fs::recursive_directory_iterator entry(modelRoot, fs::directory_options::skip_permission_denied, error);
        const fs::recursive_directory_iterator end;
        while (entry != end) {
            if (!error && entry->is_regular_file(error) && entry->path().extension() == ".gltf") {
                const fs::path relativePath = fs::relative(entry->path(), modelRoot, error);
                if (!error) {
                    models.push_back({
                        .name = entry->path().stem().string(),
                        .relativePath = (fs::path("models") / relativePath).generic_string()
                    });
                }
            }
            error.clear();
            entry.increment(error);
        }

        std::ranges::sort(models, {}, &ModelAsset::relativePath);
        return models;
    }
}
