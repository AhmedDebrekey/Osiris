#include "AssetBrowserPanel.h"

#include "renderer/Camera.h"
#include "scene/Components.h"
#include "scene/Scene.h"

#include <imgui.h>
#include <algorithm>

namespace Osiris {
    namespace {
        constexpr const char* kAssetEntryPayload = "OSIRIS_MODEL_ASSET";
        constexpr float kSpawnDistance = 3.0f;
    }

    AssetBrowserPanel::AssetBrowserPanel()
        : m_Models(AssetCatalog::ListModels()) {
    }

    void AssetBrowserPanel::Draw(Scene& scene, const Camera& camera, IRHI* rhi) {
        ImGui::Begin("Asset Browser");

        if (m_Models.empty()) {
            ImGui::TextDisabled("No .gltf models found under assets/models/.");
        }

        for (const AssetEntry& model : m_Models) {
            ImGui::PushID(model.relativePath.c_str());
            ImGui::Selectable(model.name.c_str());
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", model.relativePath.c_str());
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    SpawnModel(model, scene, camera, rhi);
                }
            }
            if (ImGui::BeginDragDropSource()) {
                ImGui::SetDragDropPayload(
                    kAssetEntryPayload,
                    model.relativePath.c_str(),
                    model.relativePath.size() + 1);
                ImGui::TextUnformatted(model.name.c_str());
                ImGui::EndDragDropSource();
            }
            ImGui::PopID();
        }

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
        for (Entity entity : scene.SpawnModel(model.name, model.relativePath, rhi)) {
            entity.GetComponent<TransformComponent>().position = spawnPosition;
        }
    }
}
