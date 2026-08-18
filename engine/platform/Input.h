//
// Created by Debreky on 21/07/2026.
//

#ifndef OSIRIS_INPUT_H
#define OSIRIS_INPUT_H
#include <glm/glm.hpp>
#include <SDL2/SDL.h>
namespace Osiris {
    class Input {
    public:
        void Update();

        bool IsKeyHeld(SDL_Scancode key) const;
        bool IsKeyPressed(SDL_Scancode key) const;

        glm::vec2 GetMouseDelta() const;
        glm::vec2 GetMousePosition() const;

        bool IsMouseButtonHeld(int button) const;

    private:
        uint8_t        m_CurrentKeys[SDL_NUM_SCANCODES]  = {};
        uint8_t        m_PreviousKeys[SDL_NUM_SCANCODES] = {};

        glm::vec2 m_MouseDelta    = {0.0f, 0.0f};
        glm::vec2 m_MousePosition = {0.0f, 0.0f};

        uint32_t m_MouseButtons = 0;
    };
}

#endif //OSIRIS_INPUT_H