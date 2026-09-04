//
// Created by Debreky on 01/06/2026.
//

#ifndef OSIRIS_VULKANTYPES_H
#define OSIRIS_VULKANTYPES_H

#include <vulkan/vulkan.h>
#include <cstdint>
#include <vector>
#include <vk_mem_alloc.h>

#include "rhi/RHITypes.h"

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
    VkFence             inFlightFence           = VK_NULL_HANDLE;
};

struct VulkanBuffer {
    VkBuffer        buffer     = VK_NULL_HANDLE;
    VmaAllocation   allocation = VK_NULL_HANDLE;
    VmaAllocationInfo allocationInfo = {};
    uint64_t        size       = 0;
};

struct VulkanImage {
    VkImage       image         = VK_NULL_HANDLE;
    VmaAllocation allocation    = VK_NULL_HANDLE;
    VkImageView   imageView     = VK_NULL_HANDLE;
    VkSampler     sampler       = VK_NULL_HANDLE;
    VkFormat      format        = VK_FORMAT_B8G8R8A8_UNORM;
};

struct VulkanMaterial {
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    MaterialDesc description;
};

// A cube-compatible image with two views into the same memory: a 2D_ARRAY
// view (6 layers) for compute imageStore, and a CUBE view for samplerCube
// reads. Single-mip only — the prefiltered environment mip chain needs a
// storage view per mip and is handled separately.
struct VulkanCubemapImage {
    VkImage       image        = VK_NULL_HANDLE;
    VmaAllocation allocation   = VK_NULL_HANDLE;
    VkImageView   storageView  = VK_NULL_HANDLE;
    VkImageView   sampledView  = VK_NULL_HANDLE;
    VkSampler     sampler      = VK_NULL_HANDLE;
    VkFormat      format       = VK_FORMAT_UNDEFINED;
    uint32_t      size         = 0;
};

#endif //OSIRIS_VULKANTYPES_H
