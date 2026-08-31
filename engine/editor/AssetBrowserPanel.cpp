#include "AssetBrowserPanel.h"

#include "core/AssetManager.h"
#include "core/Log.h"
#include "renderer/Camera.h"
#include "rhi/RHI.h"
#include "scene/Components.h"
#include "scene/Scene.h"
#include "scripting/ScriptTemplate.h"

#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string_view>

namespace Osiris {
    namespace {
        constexpr const char* kAssetEntryPayload = "OSIRIS_MODEL_ASSET";
        constexpr const char* kScriptAssetPayload = "OSIRIS_SCRIPT_ASSET";
        constexpr const char* kAudioAssetPayload = "OSIRIS_AUDIO_ASSET";
        constexpr float kSpawnDistance = 3.0f;
        constexpr float kTileSize = 64.0f;
        constexpr float kTilePadding = 16.0f;

        std::string ToLower(std::string_view text) {
            std::string result(text);
            std::ranges::transform(result, result.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return result;
        }

        std::string ParentOf(const std::string& relativePath) {
            const size_t slash = relativePath.find_last_of('/');
            return slash == std::string::npos ? std::string() : relativePath.substr(0, slash);
        }
    }

    AssetBrowserPanel::AssetBrowserPanel()
        : m_Root(AssetCatalog::BuildAssetTree()) {
    }

    void AssetBrowserPanel::Refresh() {
        m_Root = AssetCatalog::BuildAssetTree();
        if (!FindNode(m_SelectedFolder)) m_SelectedFolder.clear();
    }

    const AssetTreeNode* AssetBrowserPanel::FindNode(const std::string& relativePath) const {
        if (relativePath.empty()) return &m_Root;

        const AssetTreeNode* current = &m_Root;
        size_t start = 0;
        while (start <= relativePath.size()) {
            const size_t slash = relativePath.find('/', start);
            const std::string segment = relativePath.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
            const auto it = std::ranges::find_if(current->children,
                [&](const AssetTreeNode& n) { return n.isDirectory && n.name == segment; });
            if (it == current->children.end()) return nullptr;
            current = &*it;
            if (slash == std::string::npos) break;
            start = slash + 1;
        }
        return current;
    }

    EditorIcon AssetBrowserPanel::IconForFile(const std::string& fileName) {
        const std::string ext = std::filesystem::path(fileName).extension().string();
        if (ext == ".gltf") return EditorIcon::GltfModel;
        if (ext == ".lua") return EditorIcon::LuaScript;
        if (ext == ".json") return EditorIcon::JsonScene;
        if (ext == ".wav") return EditorIcon::WavAudio;
        return EditorIcon::Count; // no icon for this extension yet; GetEditorIconTextureID(Count) safely returns 0
    }

    const char* AssetBrowserPanel::PayloadForFile(const std::string& fileName) {
        const std::string ext = std::filesystem::path(fileName).extension().string();
        if (ext == ".gltf") return kAssetEntryPayload;
        if (ext == ".lua") return kScriptAssetPayload;
        if (ext == ".wav") return kAudioAssetPayload;
        return nullptr; // scenes (and anything else): no drop target consumes these, no drag source
    }

    void AssetBrowserPanel::OpenCreatePopup(PendingCreate kind, const std::string& parentFolder) {
        m_PendingCreate = kind;
        m_PendingCreateParent = parentFolder;
        m_NewItemNameBuffer[0] = '\0';
        m_CreatePopupRequested = true;
    }

    void AssetBrowserPanel::OpenDeleteConfirm(const std::string& relativePath, const std::string& name, bool isDirectory) {
        m_PendingDeletePath = relativePath;
        m_PendingDeleteName = name;
        m_PendingDeleteIsDirectory = isDirectory;
        m_DeletePopupRequested = true;
    }

    void AssetBrowserPanel::DrawCreatePopup() {
        if (m_CreatePopupRequested) {
            ImGui::OpenPopup("CreateAssetPopup");
            m_CreatePopupRequested = false;
        }
        if (!ImGui::BeginPopup("CreateAssetPopup")) return;

        ImGui::TextUnformatted(m_PendingCreate == PendingCreate::Folder ? "New Folder" : "New Script");
        ImGui::SetNextItemWidth(200.0f);
        const bool confirmedByEnter = ImGui::InputText("##NewItemName", m_NewItemNameBuffer,
            sizeof(m_NewItemNameBuffer), ImGuiInputTextFlags_EnterReturnsTrue);

        const bool validName = m_NewItemNameBuffer[0] != '\0';
        if (!validName) ImGui::BeginDisabled();
        const bool confirmedByButton = ImGui::Button("Create");
        if (!validName) ImGui::EndDisabled();

        if (validName && (confirmedByEnter || confirmedByButton)) {
            const std::string parentFull = AssetManager::GetPath(m_PendingCreateParent);
            if (m_PendingCreate == PendingCreate::Folder) {
                std::error_code error;
                const std::filesystem::path newFolder = std::filesystem::path(parentFull) / m_NewItemNameBuffer;
                std::filesystem::create_directory(newFolder, error);
                if (error) {
                    OSIRIS_ERROR("AssetBrowserPanel: failed to create folder '{}': {}", newFolder.string(), error.message());
                }
            } else if (m_PendingCreate == PendingCreate::Script) {
                std::string name = m_NewItemNameBuffer;
                if (!name.ends_with(".lua")) name += ".lua";
                CreateScriptFileIfMissing((std::filesystem::path(parentFull) / name).string());
            }
            Refresh();
            m_PendingCreate = PendingCreate::None;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            m_PendingCreate = PendingCreate::None;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    void AssetBrowserPanel::DrawDeleteConfirmPopup() {
        if (m_DeletePopupRequested) {
            ImGui::OpenPopup("DeleteAssetConfirm");
            m_DeletePopupRequested = false;
        }
        if (!ImGui::BeginPopup("DeleteAssetConfirm")) return;

        ImGui::Text("Delete \"%s\"?", m_PendingDeleteName.c_str());
        if (m_PendingDeleteIsDirectory) {
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "This deletes the folder and everything inside it.");
        }
        ImGui::TextDisabled("This can't be undone from the editor.");

        if (ImGui::Button("Delete", ImVec2(80.0f, 0.0f))) {
            std::error_code error;
            const std::string fullPath = AssetManager::GetPath(m_PendingDeletePath);
            if (m_PendingDeleteIsDirectory) {
                std::filesystem::remove_all(fullPath, error);
            } else {
                std::filesystem::remove(fullPath, error);
            }
            if (error) {
                OSIRIS_ERROR("AssetBrowserPanel: failed to delete '{}': {}", fullPath, error.message());
            }
            // Back out of the deleted folder (or a folder that was inside it) instead of browsing
            // a relativePath that no longer resolves to anything.
            if (m_SelectedFolder == m_PendingDeletePath
                || m_SelectedFolder.starts_with(m_PendingDeletePath + "/")) {
                m_SelectedFolder = ParentOf(m_PendingDeletePath);
            }
            Refresh();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(80.0f, 0.0f))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    void AssetBrowserPanel::DrawFolderTree(const AssetTreeNode& node) {
        for (const AssetTreeNode& child : node.children) {
            if (!child.isDirectory) continue;

            const bool hasSubfolders = std::ranges::any_of(child.children,
                [](const AssetTreeNode& n) { return n.isDirectory; });

            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
            if (!hasSubfolders) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
            if (child.relativePath == m_SelectedFolder) flags |= ImGuiTreeNodeFlags_Selected;

            ImGui::PushID(child.relativePath.c_str());
            const bool open = ImGui::TreeNodeEx(child.name.c_str(), flags);
            if (ImGui::IsItemClicked()) m_SelectedFolder = child.relativePath;

            if (ImGui::BeginPopupContextItem("FolderTreeContext")) {
                if (ImGui::MenuItem("New Folder")) OpenCreatePopup(PendingCreate::Folder, child.relativePath);
                if (ImGui::MenuItem("New Script")) OpenCreatePopup(PendingCreate::Script, child.relativePath);
                ImGui::Separator();
                if (ImGui::MenuItem("Delete")) OpenDeleteConfirm(child.relativePath, child.name, true);
                ImGui::EndPopup();
            }

            if (open && hasSubfolders) {
                DrawFolderTree(child);
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    }

    Entity AssetBrowserPanel::DrawContentGrid(const AssetTreeNode& folder, Scene& scene,
                                               const Camera& camera, IRHI* rhi) {
        Entity spawnedEntity;
        if (ImGui::BeginPopupContextWindow("ContentGridContext",
            ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
            if (ImGui::MenuItem("New Folder")) OpenCreatePopup(PendingCreate::Folder, folder.relativePath);
            if (ImGui::MenuItem("New Script")) OpenCreatePopup(PendingCreate::Script, folder.relativePath);
            ImGui::EndPopup();
        }

        const std::string filter = ToLower(m_FilterBuffer);
        const float windowVisibleX2 = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
        bool anyShown = false;

        for (const AssetTreeNode& child : folder.children) {
            if (!filter.empty() && ToLower(child.name).find(filter) == std::string::npos) continue;
            anyShown = true;

            ImGui::PushID(child.relativePath.c_str());
            ImGui::BeginGroup();

            const EditorIcon icon = child.isDirectory ? EditorIcon::Folder : IconForFile(child.name);
            const uint64_t iconID = rhi->GetEditorIconTextureID(icon);
            if (iconID != 0) {
                ImGui::ImageButton("##Tile", iconID, ImVec2(kTileSize, kTileSize));
            } else {
                ImGui::Button(child.isDirectory ? "[Dir]" : "[?]", ImVec2(kTileSize, kTileSize));
            }

            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", child.relativePath.c_str());
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    if (child.isDirectory) {
                        m_SelectedFolder = child.relativePath;
                    } else if (icon == EditorIcon::GltfModel) {
                        const AssetEntry model{std::filesystem::path(child.name).stem().string(), child.relativePath};
                        spawnedEntity = SpawnModel(model, scene, camera, rhi);
                    }
                }
            }

            if (!child.isDirectory) {
                if (const char* payloadType = PayloadForFile(child.name)) {
                    if (ImGui::BeginDragDropSource()) {
                        ImGui::SetDragDropPayload(payloadType, child.relativePath.c_str(), child.relativePath.size() + 1);
                        ImGui::TextUnformatted(child.name.c_str());
                        ImGui::EndDragDropSource();
                    }
                }
            }

            if (ImGui::BeginPopupContextItem("TileContext")) {
                if (child.isDirectory) {
                    if (ImGui::MenuItem("New Folder")) OpenCreatePopup(PendingCreate::Folder, child.relativePath);
                    if (ImGui::MenuItem("New Script")) OpenCreatePopup(PendingCreate::Script, child.relativePath);
                    ImGui::Separator();
                }
                if (ImGui::MenuItem("Delete")) OpenDeleteConfirm(child.relativePath, child.name, child.isDirectory);
                ImGui::EndPopup();
            }

            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kTileSize);
            ImGui::TextUnformatted(child.name.c_str());
            ImGui::PopTextWrapPos();
            ImGui::EndGroup();
            ImGui::PopID();

            // Standard ImGui grid-wrap idiom: keep the row going only while the next tile would
            // still fit before the window's right edge.
            const float nextTileRight = ImGui::GetItemRectMax().x + kTilePadding + kTileSize;
            if (nextTileRight < windowVisibleX2) ImGui::SameLine(0.0f, kTilePadding);
        }

        if (!anyShown) {
            ImGui::TextDisabled(folder.children.empty() ? "Empty folder." : "No matches.");
        }
        return spawnedEntity;
    }

    Entity AssetBrowserPanel::Draw(Scene& scene, const Camera& camera, IRHI* rhi) {
        Entity spawnedEntity;
        ImGui::Begin("Asset Browser");

        if (ImGui::Button("Refresh")) Refresh();
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##AssetFilter", "Search assets...", m_FilterBuffer, sizeof(m_FilterBuffer));
        ImGui::Separator();

        ImGui::BeginChild("AssetTreePane", ImVec2(200.0f, 0.0f), true);
        {
            ImGuiTreeNodeFlags rootFlags = ImGuiTreeNodeFlags_OpenOnArrow
                | ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
            if (m_SelectedFolder.empty()) rootFlags |= ImGuiTreeNodeFlags_Selected;

            const bool rootOpen = ImGui::TreeNodeEx("assets", rootFlags);
            if (ImGui::IsItemClicked()) m_SelectedFolder.clear();
            if (ImGui::BeginPopupContextItem("RootTreeContext")) {
                if (ImGui::MenuItem("New Folder")) OpenCreatePopup(PendingCreate::Folder, "");
                if (ImGui::MenuItem("New Script")) OpenCreatePopup(PendingCreate::Script, "");
                ImGui::EndPopup();
            }
            if (rootOpen) {
                DrawFolderTree(m_Root);
                ImGui::TreePop();
            }
        }
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("AssetContentPane", ImVec2(0.0f, 0.0f), true);
        if (const AssetTreeNode* current = FindNode(m_SelectedFolder)) {
            spawnedEntity = DrawContentGrid(*current, scene, camera, rhi);
        }
        ImGui::EndChild();

        DrawCreatePopup();
        DrawDeleteConfirmPopup();

        ImGui::End();
        return spawnedEntity;
    }

    Entity AssetBrowserPanel::DrawViewportDropTarget(
        Scene& scene, const Camera& camera, IRHI* rhi) const {
        Entity spawnedEntity;
        if (!ImGui::BeginDragDropTarget()) return spawnedEntity;

        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kAssetEntryPayload)) {
            const std::string relativePath = static_cast<const char*>(payload->Data);
            const AssetEntry model{std::filesystem::path(relativePath).stem().string(), relativePath};
            spawnedEntity = SpawnModel(model, scene, camera, rhi);
        }

        ImGui::EndDragDropTarget();
        return spawnedEntity;
    }

    Entity AssetBrowserPanel::SpawnModel(
        const AssetEntry& model, Scene& scene, const Camera& camera, IRHI* rhi) {
        const glm::vec3 spawnPosition = camera.GetPosition() + camera.GetFront() * kSpawnDistance;
        Entity root = scene.SpawnModel(model.name, model.relativePath, rhi);
        if (root.IsValid()) {
            root.GetComponent<TransformComponent>().position = spawnPosition;
        }
        return root;
    }
}
