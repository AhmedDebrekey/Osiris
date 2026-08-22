#include "RenderDebugPanel.h"

#include "rhi/RHI.h"

#include <imgui.h>
#include <algorithm>

namespace Osiris {
    void RenderDebugPanel::Draw(IRHI* rhi, float deltaTime) {
        ImGui::Begin("Render Debugger");

        if (ImGui::CollapsingHeader("Timings", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("CPU Frame: %.3f ms", deltaTime * 1000.0f);
            const auto timings = rhi->GetGPUTimings();
            if (timings.empty()) {
                ImGui::TextDisabled("Waiting for GPU timing results...");
            }
            for (const auto& [name, milliseconds] : timings) {
                ImGui::Text("%s: %.3f ms", name.c_str(), milliseconds);
            }
        }

        if (ImGui::CollapsingHeader("Texture Viewer", ImGuiTreeNodeFlags_DefaultOpen)) {
            constexpr const char* textureNames[] = {
                "Cascade 0", "Cascade 1", "Cascade 2",
                "Spot 0", "Spot 1", "Spot 2",
            };
            ImGui::Combo("Texture", &m_SelectedTexture, textureNames, std::size(textureNames));

            const uint64_t textureID = m_SelectedTexture < 3
                ? rhi->GetShadowCascadeTextureID(static_cast<uint32_t>(m_SelectedTexture))
                : rhi->GetSpotShadowTextureID(static_cast<uint32_t>(m_SelectedTexture - 3));
            if (textureID != 0) {
                const ImVec2 available = ImGui::GetContentRegionAvail();
                const float imageSize = std::max(1.0f, std::min(available.x, available.y));
                ImGui::Image(textureID, ImVec2(imageSize, imageSize));
            }
        }

        ImGui::End();
    }
}
