//
// Created by Debreky on 28/05/2026.
//

#ifndef OSIRIS_VULKANCONTEXT_H
#define OSIRIS_VULKANCONTEXT_H

#include <vulkan/vulkan.h>

#include "core/EngineConfig.h"

namespace Osiris {
    class VulkanContext {
        public:
        VulkanContext() = default;
        ~VulkanContext() = default;

        bool Initialize(const VulkanContextDesc& context);
        void Shutdown() const;

        private:

        bool SetupDebugMessenger();

        static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
            VkDebugUtilsMessageTypeFlagsEXT type,
            const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
            void* userData);

        bool SelectPhysicalDevice();

        bool CreateLogicalDevice();

        bool CreateSurface();

        VkInstance m_Instance = VK_NULL_HANDLE;

        VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
        VkDevice m_Device = VK_NULL_HANDLE;
        VkQueue m_GraphicsQueue = VK_NULL_HANDLE;
        uint32_t m_GraphicsQueueFamilyIndex = 0;

        VkDebugUtilsMessengerEXT m_DebugMessenger = VK_NULL_HANDLE;

        VkSurfaceKHR m_Surface = VK_NULL_HANDLE;

        VulkanContextDesc m_VulkanContextDesc;
    };
} // Osiris

#endif