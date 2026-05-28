//
// Created by Debreky on 28/05/2026.
//

#ifndef OSIRIS_VULKANCONTEXT_H
#define OSIRIS_VULKANCONTEXT_H

#include <vulkan/vulkan.h>

namespace Osiris {
    class VulkanContext {
        public:
        VulkanContext() = default;
        ~VulkanContext() = default;

        bool Initialize();
        void Shutdown() const;

        private:
        VkInstance m_Instance = VK_NULL_HANDLE;
    };
} // Osiris

#endif