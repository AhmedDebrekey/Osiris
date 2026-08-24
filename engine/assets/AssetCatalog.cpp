#include "AssetCatalog.h"

#include "core/AssetManager.h"

#include <algorithm>
#include <filesystem>

namespace Osiris {
    namespace {
        // Shared by ListModels/ListScenes — recursively finds files with extension under
        // rootRelative (an AssetManager-relative subfolder), returning paths already re-prefixed
        // with rootRelative so they're directly usable by whatever loads them (Scene::SpawnModel,
        // SceneLoader::Load, ...) without the caller needing to know the root convention.
        std::vector<AssetEntry> ListFilesWithExtension(const std::string& rootRelative, const std::string& extension) {
            namespace fs = std::filesystem;

            const fs::path root = AssetManager::GetPath(rootRelative);
            std::vector<AssetEntry> entries;

            std::error_code error;
            if (!fs::is_directory(root, error)) return entries;

            fs::recursive_directory_iterator entry(root, fs::directory_options::skip_permission_denied, error);
            const fs::recursive_directory_iterator end;
            while (entry != end) {
                if (!error && entry->is_regular_file(error) && entry->path().extension() == extension) {
                    const fs::path relativePath = fs::relative(entry->path(), root, error);
                    if (!error) {
                        entries.push_back({
                            .name = entry->path().stem().string(),
                            .relativePath = (fs::path(rootRelative) / relativePath).generic_string()
                        });
                    }
                }
                error.clear();
                entry.increment(error);
            }

            std::ranges::sort(entries, {}, &AssetEntry::relativePath);
            return entries;
        }
    }

    std::vector<AssetEntry> AssetCatalog::ListModels() {
        return ListFilesWithExtension("models", ".gltf");
    }

    std::vector<AssetEntry> AssetCatalog::ListScenes() {
        return ListFilesWithExtension("scenes", ".json");
    }

    std::vector<AssetEntry> AssetCatalog::ListScripts() {
        return ListFilesWithExtension("scripts", ".lua");
    }
}
