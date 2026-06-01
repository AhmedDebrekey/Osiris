//
// Created by Debreky on 01/06/2026.
//

#ifndef OSIRIS_VULKANTYPES_H
#define OSIRIS_VULKANTYPES_H

#include <vulkan/vulkan.h>
#include <cstdint>
#include <vector>

struct VulkanDevice {
    VkPhysicalDevice    physicalDevice  = VK_NULL_HANDLE;
    VkDevice            logicalDevice   = VK_NULL_HANDLE;
    VkQueue             graphicsQueue   = VK_NULL_HANDLE;
    VkQueue             computeQueue    = VK_NULL_HANDLE;
    VkQueue             transferQueue   = VK_NULL_HANDLE;

    uint32_t    graphicsQueueIndex  = UINT32_MAX;
    uint32_t    computeQueueIndex   = UINT32_MAX;
    uint32_t    transferQueueIndex  = UINT32_MAX;
};

struct VulkanSwapChain {
    VkSwapchainKHR              swapChain               = VK_NULL_HANDLE;
    std::vector<VkImage>        swapChainImages;
    std::vector<VkImageView>    swapChainImageViews;
    VkFormat                    swapChainImageFormat    = VK_FORMAT_B8G8R8A8_UNORM;
    VkExtent2D                  swapChainExtent;
};

struct VulkanFrameData {
    VkCommandBuffer     commandBuffer           = VK_NULL_HANDLE;
    VkSemaphore         imageAvailableSemaphore = VK_NULL_HANDLE;
    VkSemaphore         renderFinishedSemaphore = VK_NULL_HANDLE;
    VkFence             inFlightFence           = VK_NULL_HANDLE;
};

#endif //OSIRIS_VULKANTYPES_H