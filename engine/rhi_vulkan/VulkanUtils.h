//
// Created by Debreky on 31/05/2026.
//

#ifndef OSIRIS_VULKANUTILS_H
#define OSIRIS_VULKANUTILS_H

#define VK_CHECK(x) \
    if (x != VK_SUCCESS) { \
    OSIRIS_ERROR("Vulkan error at line {}", __LINE__); \
    }

#endif //OSIRIS_VULKANUTILS_H