//
// Created by ahtal on 28/05/2026.
//

#include "VulkanContext.h"

#include "core/Log.h"

namespace Osiris {
    bool VulkanContext::Initialize() {
        VkApplicationInfo appInfo{
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .pApplicationName = "Osiris",
            .applicationVersion = VK_MAKE_VERSION(0, 1, 0),
            .apiVersion = VK_API_VERSION_1_4,
        };

        VkInstanceCreateInfo createInfo{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &appInfo,
        };


        const VkResult result = vkCreateInstance(&createInfo, nullptr, &m_Instance);
        if (result != VK_SUCCESS) {
            OSIRIS_ERROR("Failed to create Vulkan instance!");
            return false;
        }

        OSIRIS_INFO("Vulkan instance created!");

        return true;
    }

    void VulkanContext::Shutdown() const {
        vkDestroyInstance(m_Instance, nullptr);
    }
} // Osiris