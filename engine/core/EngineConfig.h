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
#endif //OSIRIS_ENGINECONFIG_H