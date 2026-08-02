//
// Created by Debreky on 28/05/2026.
//

#include "Window.h"
#include <SDL2/SDL.h>

#include "core/Log.h"

#include <backends/imgui_impl_sdl2.h>

namespace Osiris {

    bool Window::Initialize(const WindowDesc &desc) {
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
            OSIRIS_ERROR("Failed to initialise SDL2: {}", SDL_GetError());
            return false;
        }

        m_Width = desc.width;
        m_Height = desc.height;
        m_Title = desc.title;

        Uint32 flags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE;
        if (desc.fullscreen)
            flags |= SDL_WINDOW_FULLSCREEN;

        m_Window = SDL_CreateWindow(
            m_Title.c_str(),
            SDL_WINDOWPOS_CENTERED,
            SDL_WINDOWPOS_CENTERED,
            m_Width,
            m_Height,
            flags);

        if (!m_Window) {
            OSIRIS_ERROR("Failed to create window: {}", SDL_GetError());
            return false;
        }
        return true;
    }

    void Window::PollEvents() {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            switch (event.type)
            {
                case SDL_QUIT:
                    m_ShouldClose = true;
                    break;

                case SDL_WINDOWEVENT:
                    if (event.window.event == SDL_WINDOWEVENT_RESIZED)
                    {
                        m_Width  = static_cast<uint32_t>(event.window.data1);
                        m_Height = static_cast<uint32_t>(event.window.data2);
                        OSIRIS_INFO("Resized Window: {}", m_Width, m_Height );
                    }
                    break;

                case SDL_KEYDOWN:
                    if (event.key.keysym.sym == SDLK_ESCAPE)
                        m_ShouldClose = true;
                    break;

                default:
                    break;
            }
        }
    }

    void Window::Shutdown() {
        if (m_Window) {
            SDL_DestroyWindow(m_Window);
            m_Window = nullptr;
        }

        SDL_Quit();
    }
} // Osiris