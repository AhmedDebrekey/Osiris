#include "AssetBrowserPanel.h"

#include "renderer/Camera.h"
#include "rhi/RHI.h"
#include "scene/Components.h"
#include "scene/Scene.h"

#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <string_view>

namespace Osiris {
    namespace {
        constexpr const char* kAssetEntryPayload = "OSIRIS_MODEL_ASSET";
        constexpr const char* kScriptAssetPayload = "OSIRIS_SCRIPT_ASSET";
        constexpr float kSpawnDistance = 3.0f;
        constexpr float kIconSize = 16.0f;

        std::string ToLower(std::string_view text) {
            std::string result(text);
            std::ranges::transform(result, result.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return result;
        }
    }

    AssetBrowserPanel::AssetBrowserPanel()
        : m_Models(AssetCatalog::ListModels()),
          m_Scripts(AssetCatalog::ListScripts()),
          m_Scenes(AssetCatalog::ListScenes()) {
    }

    void AssetBrowserPanel::DrawAssetSection(const char* label, const std::vector<AssetEntry>& entries,
        EditorIcon icon, const char* dragPayloadType, IRHI* rhi,
        const std::function<void(const AssetEntry&)>& onDoubleClick) const {
        ImGui::PushID(label);

        const uint64_t folderIconID = rhi->GetEditorIconTextureID(EditorIcon::Folder);
        if (folderIconID != 0) {
            ImGui::Image(folderIconID, ImVec2(kIconSize, kIconSize));
            ImGui::SameLine();
        }
        const bool open = ImGui::CollapsingHeader(label);

        if (open) {
            const uint64_t rowIconID = rhi->GetEditorIconTextureID(icon);
            const std::string filter = ToLower(m_FilterBuffer);
            bool anyShown = false;

            for (const AssetEntry& entry : entries) {
                if (!filter.empty() && ToLower(entry.name).find(filter) == std::string::npos) continue;
                anyShown = true;

                ImGui::PushID(entry.relativePath.c_str());
                if (rowIconID != 0) {
                    ImGui::Image(rowIconID, ImVec2(kIconSize, kIconSize));
                    ImGui::SameLine();
                }
                ImGui::Selectable(entry.name.c_str());
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", entry.relativePath.c_str());
                    if (onDoubleClick && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        onDoubleClick(entry);
                    }
                }
                if (dragPayloadType && ImGui::BeginDragDropSource()) {
                    ImGui::SetDragDropPayload(
                        dragPayloadType, entry.relativePath.c_str(), entry.relativePath.size() + 1);
                    ImGui::TextUnformatted(entry.name.c_str());
                    ImGui::EndDragDropSource();
                }
                ImGui::PopID();
            }

            if (!anyShown) {
                ImGui::TextDisabled(entries.empty() ? "None found." : "No matches.");
            }
        }

        ImGui::PopID();
    }

    void AssetBrowserPanel::Draw(Scene& scene, const Camera& camera, IRHI* rhi) {
        ImGui::Begin("Asset Browser");

        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##AssetFilter", "Search assets...", m_FilterBuffer, sizeof(m_FilterBuffer));
        ImGui::Separator();

        DrawAssetSection("Models", m_Models, EditorIcon::GltfModel, kAssetEntryPayload, rhi,
            [&](const AssetEntry& model) { SpawnModel(model, scene, camera, rhi); });
        DrawAssetSection("Scripts", m_Scripts, EditorIcon::LuaScript, kScriptAssetPayload, rhi, nullptr);
        DrawAssetSection("Scenes", m_Scenes, EditorIcon::JsonScene, nullptr, rhi, nullptr);

        ImGui::End();
    }

    void AssetBrowserPanel::DrawViewportDropTarget(Scene& scene, const Camera& camera, IRHI* rhi) const {
        if (!ImGui::BeginDragDropTarget()) return;

        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kAssetEntryPayload)) {
            const char* relativePath = static_cast<const char*>(payload->Data);
            const auto model = std::ranges::find(m_Models, relativePath, &AssetEntry::relativePath);
            if (model != m_Models.end()) {
                SpawnModel(*model, scene, camera, rhi);
            }
        }

        ImGui::EndDragDropTarget();
    }

    void AssetBrowserPanel::SpawnModel(
        const AssetEntry& model, Scene& scene, const Camera& camera, IRHI* rhi) {
        const glm::vec3 spawnPosition = camera.GetPosition() + camera.GetFront() * kSpawnDistance;
        Entity root = scene.SpawnModel(model.name, model.relativePath, rhi);
        if (root.IsValid()) {
            root.GetComponent<TransformComponent>().position = spawnPosition;
        }
    }
}
