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

        constexpr const char* textureNames[] = {
            "Cascade 0", "Cascade 1", "Cascade 2",
            "Spot 0", "Spot 1", "Spot 2",
            "Post-Process Preview",
        };
        constexpr int postProcessIndex = static_cast<int>(std::size(textureNames)) - 1;

        // Evaluated every call, not just while the header below is expanded, so collapsing it
        // while "Post-Process Preview" is selected doesn't leave this pass stuck running forever
        // with nothing left to display it. Only actually generated while selected either way,
        // unlike the shadow maps (which render every frame regardless, needed for real lighting):
        // this pass exists purely for this preview, no reason to pay for it unless it's shown.
        rhi->GetPostProcessPreviewEnabled() = (m_SelectedTexture == postProcessIndex);

        if (ImGui::CollapsingHeader("Texture Viewer", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Combo("Texture", &m_SelectedTexture, textureNames, std::size(textureNames));

            uint64_t textureID;
            if (m_SelectedTexture == postProcessIndex) {
                textureID = rhi->GetPostProcessPreviewTextureID();
            } else if (m_SelectedTexture < 3) {
                textureID = rhi->GetShadowCascadeTextureID(static_cast<uint32_t>(m_SelectedTexture));
            } else {
                textureID = rhi->GetSpotShadowTextureID(static_cast<uint32_t>(m_SelectedTexture - 3));
            }
            if (textureID != 0) {
                const ImVec2 available = ImGui::GetContentRegionAvail();
                const float imageSize = std::max(1.0f, std::min(available.x, available.y));
                ImGui::Image(textureID, ImVec2(imageSize, imageSize));
            }
        }

        ImGui::End();
    }
}
