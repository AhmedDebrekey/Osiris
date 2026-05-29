//
// Created by Debreky on 28/05/2026.
//

#ifndef OSIRIS_ENGINECONFIG_H
#define OSIRIS_ENGINECONFIG_H
#include <string>

struct WindowDesc {
    std::string title = "Osiris";
    uint32_t width = 1280;
    uint32_t height = 720;
    bool vsync = true;
    bool fullscreen = false;
};

struct SDL_Window;
struct VulkanContextDesc {
    SDL_Window* windowHandle = nullptr;
    uint32_t windowWidth = 1280;
    uint32_t windowHeight = 720;
};
#endif //OSIRIS_ENGINECONFIG_H