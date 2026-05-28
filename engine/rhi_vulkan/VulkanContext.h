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

        bool SetupDebugMessenger();

        static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
            VkDebugUtilsMessageTypeFlagsEXT type,
            const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
            void* userData);

        bool SelectPhysicalDevice();

        VkInstance m_Instance = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT m_DebugMessenger = VK_NULL_HANDLE;

        VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
    };
} // Osiris

#endif