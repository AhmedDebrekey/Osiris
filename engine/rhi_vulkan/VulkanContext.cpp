//
// Created by ahtal on 28/05/2026.
//

#include "VulkanContext.h"

#include "core/Log.h"

namespace Osiris {
    bool VulkanContext::Initialize()
    {
        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "Osiris";
        appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
        appInfo.apiVersion = VK_API_VERSION_1_4;

        VkInstanceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;

#ifndef NDEBUG
        const char* validationLayers[] = {
            "VK_LAYER_KHRONOS_validation"
        };
        createInfo.enabledLayerCount = 1;
        createInfo.ppEnabledLayerNames = validationLayers;

        const char* extensions[] = {
            VK_EXT_DEBUG_UTILS_EXTENSION_NAME
        };
        createInfo.enabledExtensionCount = 1;
        createInfo.ppEnabledExtensionNames = extensions;
#endif

        VkResult result = vkCreateInstance(&createInfo, nullptr, &m_Instance);
        if (result != VK_SUCCESS)
        {
            OSIRIS_ERROR("Failed to create Vulkan instance!");
            return false;
        }

        OSIRIS_INFO("Vulkan instance created!");

#ifndef NDEBUG
        if (!SetupDebugMessenger())
        {
            OSIRIS_ERROR("Failed to setup debug messenger!");
            return false;
        }
#endif

        return true;
    }

    void VulkanContext::Shutdown() const {
        vkDestroyInstance(m_Instance, nullptr);
    }

    bool VulkanContext::SetupDebugMessenger() {
        VkDebugUtilsMessengerCreateInfoEXT createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                     VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
        createInfo.pfnUserCallback = DebugCallback;

        const auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(m_Instance, "vkCreateDebugUtilsMessengerEXT");
        if (!func) {
            OSIRIS_ERROR("Failed to load vkCreateDebugUtilsMessengerEXT");
            return false;
        }

        VkResult result = func(m_Instance, &createInfo, nullptr, &m_DebugMessenger);
        if (result != VK_SUCCESS) {
            OSIRIS_ERROR("Failed to create Vulkan messenger!");
            return false;
        }
        OSIRIS_INFO("Vulkan debug messenger created!");
        return true;
    }

    VkBool32 VulkanContext::DebugCallback(
    const VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
    void* userData)
    {
        if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
            OSIRIS_ERROR("Vulkan: {}", callbackData->pMessage);
        else if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
            OSIRIS_WARN("Vulkan: {}", callbackData->pMessage);
        else
            OSIRIS_INFO("Vulkan: {}", callbackData->pMessage);

        return VK_FALSE;
    }
} // Osiris