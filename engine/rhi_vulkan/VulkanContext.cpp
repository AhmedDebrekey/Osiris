//
// Created by ahtal on 28/05/2026.
//

#include "VulkanContext.h"
#include "core/Log.h"

#include <SDL_vulkan.h>
#include <SDL2/SDL.h>

namespace Osiris {
    bool VulkanContext::Initialize(const VulkanContextDesc& context)
    {

        m_VulkanContextDesc = context;

        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "Osiris";
        appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
        appInfo.apiVersion = VK_API_VERSION_1_4;

        uint32_t sdlExtensionCount = 0;
        SDL_Vulkan_GetInstanceExtensions(m_VulkanContextDesc.windowHandle, &sdlExtensionCount, nullptr);
        if (sdlExtensionCount == 0) {
            OSIRIS_ERROR("Vulkan extensions not found!");
            return false;
        }

        std::vector<const char*> extensions_names(sdlExtensionCount);
        SDL_Vulkan_GetInstanceExtensions(m_VulkanContextDesc.windowHandle, &sdlExtensionCount, extensions_names.data());
        if (extensions_names.empty()) {
            OSIRIS_ERROR("Couldn't get Vulkan Extensions");
            return false;
        }

#ifndef NDEBUG
        extensions_names.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

        VkInstanceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;
        createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions_names.size());
        createInfo.ppEnabledExtensionNames = extensions_names.data();

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
        SelectPhysicalDevice();
        CreateLogicalDevice();
        CreateSurface();
        CreateSwapChain();

        return true;
    }

    void VulkanContext::Shutdown() const {
        vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);
        vkDestroySwapchainKHR(m_Device, m_Swapchain, nullptr);
        vkDestroyDevice(m_Device, nullptr);
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

    bool VulkanContext::SelectPhysicalDevice() {
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(m_Instance, &deviceCount, nullptr);
        if (deviceCount == 0) {
            OSIRIS_ERROR("Vulkan found no physical devices (GPUS)");
            return false;
        }
        std::vector<VkPhysicalDevice> physicalDevices(deviceCount);
        vkEnumeratePhysicalDevices(m_Instance, &deviceCount, physicalDevices.data());

        for (auto device : physicalDevices) {
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(device, &props);
            if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                OSIRIS_INFO("GPU: {}", props.deviceName);
                m_PhysicalDevice = device;
                return true;
            }
        }
        OSIRIS_INFO("Using Integrated GPU");
        m_PhysicalDevice = physicalDevices[0];
        return true;
    }

    bool VulkanContext::CreateLogicalDevice() {
        uint32_t queueCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(m_PhysicalDevice, &queueCount, nullptr);
        if (queueCount == 0) {
            OSIRIS_ERROR("Vulkan found no queue families");
            return false;
        }
        OSIRIS_INFO("Queue count : {}", queueCount);
        std::vector<VkQueueFamilyProperties> queueFamilies(queueCount);
        vkGetPhysicalDeviceQueueFamilyProperties(m_PhysicalDevice, &queueCount, queueFamilies.data());

        for (uint32_t i = 0; i < queueCount; i++) {
            if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                m_GraphicsQueueFamilyIndex = i;
                break;
            }
        }
        float queuePriority = 1.0f;
        VkDeviceQueueCreateInfo queueCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = m_GraphicsQueueFamilyIndex,
            .queueCount = 1,
            .pQueuePriorities = &queuePriority,
        };

       VkPhysicalDeviceFeatures physicalDeviceFeatures = {};
        const char* deviceExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        VkDeviceCreateInfo deviceCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            .queueCreateInfoCount = 1,
            .pQueueCreateInfos = &queueCreateInfo,
            .enabledExtensionCount = 1,
            .ppEnabledExtensionNames = deviceExtensions,
            .pEnabledFeatures = &physicalDeviceFeatures
        };

        VkResult result = vkCreateDevice(m_PhysicalDevice, &deviceCreateInfo, nullptr, &m_Device);
        if (result != VK_SUCCESS) {
            OSIRIS_ERROR("Failed to create logical device!");
        }
        vkGetDeviceQueue(m_Device, m_GraphicsQueueFamilyIndex, 0, &m_GraphicsQueue);
        OSIRIS_INFO("Graphics queue created!");
        return true;
    }

    bool VulkanContext::CreateSurface() {
        if (!SDL_Vulkan_CreateSurface(m_VulkanContextDesc.windowHandle, m_Instance, &m_Surface)) {
            OSIRIS_ERROR("Failed to create Vulkan surface!");
            return false;
        }
        OSIRIS_INFO("Vulkan surface created!");
        return true;
    }

    bool VulkanContext::CreateSwapChain() {
        VkSurfaceCapabilitiesKHR surfaceCapabilities;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_PhysicalDevice, m_Surface, &surfaceCapabilities);

        uint32_t formatCount;
        vkGetPhysicalDeviceSurfaceFormatsKHR(m_PhysicalDevice, m_Surface, &formatCount, nullptr);
        if (formatCount == 0) {
            OSIRIS_ERROR("Vulkan found no surface formats");
            return false;
        }
        std::vector<VkSurfaceFormatKHR> surfaceFormats(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(m_PhysicalDevice, m_Surface, &formatCount, surfaceFormats.data());

        VkSurfaceFormatKHR surfaceFormat = surfaceFormats.at(0);
        for (const auto format : surfaceFormats) {
            if (format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR
                &&
                format.format == VK_FORMAT_B8G8R8A8_SRGB ) {
                surfaceFormat = format;
                OSIRIS_INFO("Vulkan found surface format");
            }
        }

        uint32_t presentModeCount;
        vkGetPhysicalDeviceSurfacePresentModesKHR(m_PhysicalDevice, m_Surface, &presentModeCount, nullptr);
        if (presentModeCount == 0) {
            OSIRIS_ERROR("Vulkan found no present modes");
            return false;
        }
        std::vector<VkPresentModeKHR> presentModes(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(m_PhysicalDevice, m_Surface, &presentModeCount, presentModes.data());

        VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
        for (const auto mode : presentModes) {
            if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
                presentMode = mode;
                OSIRIS_INFO("Vulkan found present mode Mailbox");
                break;
            }
        }

        VkExtent2D extent = surfaceCapabilities.currentExtent;

        VkSwapchainCreateInfoKHR swapchainCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
            .surface = m_Surface,
            .minImageCount = surfaceCapabilities.minImageCount + 1,
            .imageFormat = surfaceFormat.format,
            .imageColorSpace = surfaceFormat.colorSpace,
            .imageExtent = extent,
            .imageArrayLayers = 1,
            .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            .preTransform = surfaceCapabilities.currentTransform,
            .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
            .presentMode = presentMode,
            .clipped = VK_TRUE,

        };

        VkResult result = vkCreateSwapchainKHR(m_Device, &swapchainCreateInfo, nullptr, &m_Swapchain);
        if (result != VK_SUCCESS) {
            OSIRIS_ERROR("Failed to create swapchain!");
            return false;
        }

        OSIRIS_INFO("SwapChain Created!");
        return true;
    }
} // Osiris