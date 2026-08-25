#ifndef OSIRIS_GAMEUI_H
#define OSIRIS_GAMEUI_H

#include <glm/glm.hpp>
#include <string>

namespace Osiris {
    // Where the x/y position passed to Draw* sits relative to the drawn content.
    enum class UIAnchor {
        TopLeft, TopCenter, TopRight,
        CenterLeft, Center, CenterRight,
        BottomLeft, BottomCenter, BottomRight,
    };

    // Immediate-mode game-facing UI: draws straight onto ImGui's foreground draw list, with no
    // ImGui window/title bar/chrome involved, so nothing reads as "editor" to the player. Only
    // valid to call between IRHI::BeginImGuiFrame() and IRHI::RenderImGui(). Engine::RunFrame
    // already satisfies that for every script's IScripting::Update() call while playing.
    namespace GameUI {
        // x/y are normalized [0,1] fractions of the current viewport, anchored per `anchor`.
        // fontSize is a target pixel height; ImGui's default font is soft-scaled to it (no
        // custom font is baked yet, see LuaScripting.cpp's "ui" table binding).
        void DrawText(float x, float y, UIAnchor anchor, const std::string& text,
                      const glm::vec4& color, float fontSize);

        // w/h are also normalized [0,1] fractions of the viewport.
        void DrawRect(float x, float y, float w, float h, UIAnchor anchor, const glm::vec4& color);
    }
}

#endif //OSIRIS_GAMEUI_H
