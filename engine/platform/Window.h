//
// Created by Debreky on 28/05/2026.
//

#ifndef OSIRIS_WINDOW_H
#define OSIRIS_WINDOW_H
#include <string>

#include "core/EngineConfig.h"

struct SDL_Window;

namespace Osiris {


    class Window {
    public:
        Window() = default;

        ~Window() = default;

        bool Initialize(const WindowDesc& desc);

        void PollEvents();

        void Shutdown();

        bool ShouldClose() const {return m_ShouldClose;}

        uint32_t GetWidth() const {return m_Width;}

        uint32_t GetHeight() const {return m_Height;}

        SDL_Window* GetNativeWindow() const {return m_Window;}

    private:
        SDL_Window* m_Window = nullptr;
        uint32_t m_Width = 0;
        uint32_t m_Height = 0;
        bool m_ShouldClose = false;

    };
} // Osiris

#endif //OSIRIS_WINDOW_H