#include "AssetCatalog.h"

#include "core/AssetManager.h"

#include <algorithm>
#include <filesystem>

namespace Osiris {
    namespace {
        // Shared by ListScenes/BuildAssetTree: recursively finds files with extension under
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

        // Depth-first scan of one directory level, recursing into subdirectories. Folders sort
        // before files, both alphabetically, so the Asset Browser's tree/grid don't need to
        // re-sort anything themselves.
        AssetTreeNode ScanDirectory(const std::filesystem::path& fullPath,
            const std::string& relativePath, const std::string& name) {
            namespace fs = std::filesystem;

            AssetTreeNode node;
            node.name = name;
            node.relativePath = relativePath;
            node.isDirectory = true;

            std::error_code error;
            std::vector<fs::directory_entry> entries;
            for (const auto& entry : fs::directory_iterator(fullPath, fs::directory_options::skip_permission_denied, error)) {
                entries.push_back(entry);
            }
            std::ranges::sort(entries, {}, [](const fs::directory_entry& e) { return e.path().filename().string(); });

            for (const auto& entry : entries) {
                const std::string childName = entry.path().filename().string();
                const std::string childRelative = relativePath.empty() ? childName : relativePath + "/" + childName;
                if (entry.is_directory(error)) {
                    node.children.push_back(ScanDirectory(entry.path(), childRelative, childName));
                } else if (entry.is_regular_file(error)) {
                    AssetTreeNode fileNode;
                    fileNode.name = childName;
                    fileNode.relativePath = childRelative;
                    fileNode.isDirectory = false;
                    node.children.push_back(std::move(fileNode));
                }
                error.clear();
            }
            std::ranges::stable_partition(node.children, [](const AssetTreeNode& n) { return n.isDirectory; });
            return node;
        }
    }

    std::vector<AssetEntry> AssetCatalog::ListScenes() {
        return ListFilesWithExtension("scenes", ".json");
    }

    AssetTreeNode AssetCatalog::BuildAssetTree() {
        return ScanDirectory(AssetManager::GetPath(""), "", "assets");
    }
}
