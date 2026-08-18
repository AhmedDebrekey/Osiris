//
// Created by ahtal on 21/07/2026.
//

#include "Input.h"

namespace Osiris {

    void Input::Update() {
        // Snapshot last frame's state BEFORE refreshing m_CurrentKeys — SDL_GetKeyboardState
        // returns a pointer to its own internal buffer (already mutated by this frame's
        // SDL_PollEvent calls by the time Update() runs), so it can't be read twice across
        // frames to recover "previous" state; m_CurrentKeys must be an owned copy.
        memcpy(m_PreviousKeys, m_CurrentKeys, SDL_NUM_SCANCODES);
        const uint8_t* keys = SDL_GetKeyboardState(nullptr);
        memcpy(m_CurrentKeys, keys, SDL_NUM_SCANCODES);

        int dx, dy;
        SDL_GetRelativeMouseState(&dx, &dy);
        m_MouseDelta = { static_cast<float>(dx), static_cast<float>(dy) };

        int mx, my;
        SDL_GetMouseState(&mx, &my);
        m_MousePosition = { static_cast<float>(mx), static_cast<float>(my) };

        m_MouseButtons = SDL_GetMouseState(nullptr, nullptr);
    }

    bool Input::IsKeyHeld(SDL_Scancode key) const {
        return m_CurrentKeys[key] == 1;
    }

    bool Input::IsKeyPressed(SDL_Scancode key) const {
        return m_CurrentKeys[key] == 1 && m_PreviousKeys[key] == 0;
    }

    glm::vec2 Input::GetMouseDelta() const {
        return m_MouseDelta;
    }

    glm::vec2 Input::GetMousePosition() const {
        return m_MousePosition;
    }

    bool Input::IsMouseButtonHeld(int button) const {
        return (m_MouseButtons & SDL_BUTTON(button)) != 0;
    }
} // Osiris