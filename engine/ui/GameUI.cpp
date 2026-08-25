#include "GameUI.h"

#include <imgui.h>

namespace {
    using Osiris::UIAnchor;

    // Fraction of the drawn content's width/height to pull back from `pos` on each axis.
    ImVec2 AnchorFraction(UIAnchor anchor) {
        switch (anchor) {
            case UIAnchor::TopLeft:      return ImVec2(0.0f, 0.0f);
            case UIAnchor::TopCenter:    return ImVec2(0.5f, 0.0f);
            case UIAnchor::TopRight:     return ImVec2(1.0f, 0.0f);
            case UIAnchor::CenterLeft:   return ImVec2(0.0f, 0.5f);
            case UIAnchor::Center:       return ImVec2(0.5f, 0.5f);
            case UIAnchor::CenterRight:  return ImVec2(1.0f, 0.5f);
            case UIAnchor::BottomLeft:   return ImVec2(0.0f, 1.0f);
            case UIAnchor::BottomCenter: return ImVec2(0.5f, 1.0f);
            case UIAnchor::BottomRight:  return ImVec2(1.0f, 1.0f);
        }
        return ImVec2(0.5f, 0.5f);
    }
}

namespace Osiris::GameUI {
    void DrawText(float x, float y, UIAnchor anchor, const std::string& text,
                  const glm::vec4& color, float fontSize) {
        if (text.empty()) return;

        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        ImFont* font = ImGui::GetFont();

        // CalcTextSize measures at the currently active font's baked size; scale the result to
        // the caller's requested pixel size rather than pulling in imgui_internal.h just for
        // ImFont::CalcTextSizeA.
        const ImVec2 nativeSize = ImGui::CalcTextSize(text.c_str());
        const float scale = fontSize / ImGui::GetFontSize();
        const ImVec2 textSize(nativeSize.x * scale, nativeSize.y * scale);

        const ImVec2 anchorFrac = AnchorFraction(anchor);
        ImVec2 pos(x * display.x, y * display.y);
        pos.x -= textSize.x * anchorFrac.x;
        pos.y -= textSize.y * anchorFrac.y;

        const ImU32 col = ImGui::ColorConvertFloat4ToU32(ImVec4(color.r, color.g, color.b, color.a));
        drawList->AddText(font, fontSize, pos, col, text.c_str());
    }

    void DrawRect(float x, float y, float w, float h, UIAnchor anchor, const glm::vec4& color) {
        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        const ImVec2 display = ImGui::GetIO().DisplaySize;

        const ImVec2 size(w * display.x, h * display.y);
        const ImVec2 anchorFrac = AnchorFraction(anchor);
        ImVec2 pos(x * display.x, y * display.y);
        pos.x -= size.x * anchorFrac.x;
        pos.y -= size.y * anchorFrac.y;

        const ImU32 col = ImGui::ColorConvertFloat4ToU32(ImVec4(color.r, color.g, color.b, color.a));
        drawList->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), col);
    }
}
