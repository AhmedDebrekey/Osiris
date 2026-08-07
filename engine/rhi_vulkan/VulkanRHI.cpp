//
// Created by Debreky on 01/06/2026.
//
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
#include "VulkanRHI.h"

#include <fstream>
#include <SDL_vulkan.h>

#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_vulkan.h"

#include "VulkanUtils.h"
#include "core/Log.h"
#include "renderer/MeshType.h"

#include <glm/gtc/matrix_transform.hpp>


namespace Osiris {

    template<typename T>
    uint32_t AllocateSlot(std::vector<T>& slots, const T& item, auto isNull) {
        for (uint32_t i = 0; i < slots.size(); i++) {
            if (isNull(slots[i])) {
                slots[i] = item;
                return i;
            }
        }
        slots.push_back(item);
        return static_cast<uint32_t>(slots.size() - 1);
    }

    void VulkanRHI::Configure(const VulkanContextDesc &desc) {
        m_Desc = desc;
    }

    bool VulkanRHI::Init() {

        VkApplicationInfo appInfo = {
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .pApplicationName = m_Desc.windowTitle.c_str(),
            .applicationVersion = VK_MAKE_VERSION(0, 1, 0),
            .apiVersion = VK_API_VERSION_1_4,
        };

        uint32_t sdlExtensionCount = 0;
        SDL_Vulkan_GetInstanceExtensions(m_Desc.windowHandle, &sdlExtensionCount, nullptr);
        if (sdlExtensionCount == 0) {
            OSIRIS_ERROR("Vulkan extensions not found!");
            return false;
        }
        std::vector<const char *> sdlExtensions(sdlExtensionCount);
        SDL_Vulkan_GetInstanceExtensions(m_Desc.windowHandle, &sdlExtensionCount, sdlExtensions.data());
        if (sdlExtensions.empty()) {
            OSIRIS_ERROR("Vulkan extensions not found! Count: {}", sdlExtensionCount);
            return false;
        }

#ifndef NDEBUG
        sdlExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

        VkInstanceCreateInfo createInfo = {
            .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .pApplicationInfo = &appInfo,
            .enabledExtensionCount = static_cast<uint32_t>(sdlExtensions.size()),
            .ppEnabledExtensionNames = sdlExtensions.data(),
        };

#ifndef NDEBUG
        const char* validationLayers[] = { "VK_LAYER_KHRONOS_validation" };
        createInfo.enabledLayerCount = 1;
        createInfo.ppEnabledLayerNames = validationLayers;
#endif

        const VkResult result = vkCreateInstance(&createInfo, nullptr, &m_Instance);
        VK_CHECK(result)

#ifndef NDEBUG
        if (!SetupDebugMessenger()) {
            OSIRIS_ERROR("Failed to setup debug messenger!");
            return false;
        }
#endif
        if (!SelectPhysicalDevice()) {
            OSIRIS_ERROR("Failed to select physical device!");
            return false;
        }

        if (!CreateLogicalDevice()) {
            OSIRIS_ERROR("Failed to create logical device!");
            return false;
        }

        if (!CreateSurface()) {
            OSIRIS_ERROR("Failed to create surface!");
            return false;
        }

        if (!CreateSwapChain()) {
            OSIRIS_ERROR("Failed to create swap chain");
            return false;
        }

        if (!CreateSwapChainImages()) {
            OSIRIS_ERROR("Failed to create swap chain images!");
            return false;
        }

        if (!CreateDepthResources()) {
            OSIRIS_ERROR("Failed to create depth resources!");
            return false;
        }

        if (!CreateDescriptorSetLayouts()) {
            OSIRIS_ERROR("Failed to create descriptor set layouts!");
            return false;
        }

        m_PipelineManager = std::make_unique<PipelineManager>(m_Device.logicalDevice);

        VkDescriptorSetLayout forwardLayouts[] = { m_FrameDescriptorLayout, m_MaterialDescriptorLayout };

        m_ForwardPipeline = m_PipelineManager->GetOrCreate({
            .vertexShader     = "assets/shaders/triangle.vert.spv",
            .fragmentShader   = "assets/shaders/triangle.frag.spv",
            .colorAttachment  = true,
            .colorFormat      = m_SwapChain.swapChainImageFormat,
            .depthAttachment  = true,
            .depthFormat      = VK_FORMAT_D32_SFLOAT,
            .depthTest        = true,
            .depthWrite       = true,
            .depthBias        = false,
            .cullMode         = VK_CULL_MODE_BACK_BIT,
            .frontFace        = VK_FRONT_FACE_CLOCKWISE,
            .setLayoutCount   = 2,
            .pSetLayouts      = forwardLayouts,
            .pushConstantSize = sizeof(glm::mat4),
            .pushConstantStages = VK_SHADER_STAGE_VERTEX_BIT,
        });

        m_ForwardPipelineLayout = m_PipelineManager->GetLayout(m_ForwardPipeline);

        VkDescriptorSetLayout shadowLayouts[] = { m_FrameDescriptorLayout };

        m_ShadowPipeline = m_PipelineManager->GetOrCreate({
            .vertexShader       = "assets/shaders/shadow.vert.spv",
            .fragmentShader     = "",
            .colorAttachment    = false,
            .colorFormat        = VK_FORMAT_UNDEFINED,
            .depthAttachment    = true,
            .depthFormat        = VK_FORMAT_D32_SFLOAT,
            .depthTest          = true,
            .depthWrite         = true,
            .depthBias          = true,
            .depthBiasConstant  = 0.0f,
            .depthBiasClamp     = 0.0f,
            .depthBiasSlope     = 0.1f,
            .cullMode           = VK_CULL_MODE_FRONT_BIT,
            .frontFace          = VK_FRONT_FACE_CLOCKWISE,
            .setLayoutCount     = 1,
            .pSetLayouts        = shadowLayouts,
            .pushConstantSize   = sizeof(glm::mat4) * 2,
            .pushConstantStages = VK_SHADER_STAGE_VERTEX_BIT,
        });

        m_ShadowPipelineLayout = m_PipelineManager->GetLayout(m_ShadowPipeline);

        if (!CreateDescriptorPool()) {
            OSIRIS_ERROR("Failed to create descriptor pool!");
            return false;
        }

        if (!CreateDescriptorSet()) {
            OSIRIS_ERROR("Failed to create descriptor set!");
            return false;
        }

        if (!CreateCommandBuffers()) {
            OSIRIS_ERROR("Failed to create command buffers!");
            return false;
        }

        if (!CreateShadowMap()) {
            OSIRIS_ERROR("Failed to create Shadow Map");
            return false;
        }

        UpdateShadowDescriptors();

        m_ColorBufferRG = RGTexture{0};
        m_DepthBufferRG = RGTexture{1};

        return true;
    }

    void VulkanRHI::Shutdown() {
        OSIRIS_INFO("VulkanRHI shutting down...");
        vkDeviceWaitIdle(m_Device.logicalDevice);

        for (const auto& semaphore : m_ImageAvailableSemaphores) {
            vkDestroySemaphore(m_Device.logicalDevice, semaphore, nullptr);
        }
        for (const auto& semaphore : m_RenderFinishedSemaphores) {
            vkDestroySemaphore(m_Device.logicalDevice, semaphore, nullptr);
        }
        for (const auto& frame : m_Frames) {
            vkDestroyFence(m_Device.logicalDevice, frame.inFlightFence, nullptr);
        }

        for (uint32_t i = 0; i < m_Buffers.size(); i++) {
            if (m_Buffers[i].buffer != VK_NULL_HANDLE)
                DestroyBuffer(BufferHandle{i});
        }

        for (uint32_t i = 0; i < m_Textures.size(); i++) {
            if (m_Textures[i].image != VK_NULL_HANDLE)
                DestroyTexture(TextureHandle{i});
        }

        for (auto& shadowMap : m_ShadowMaps) {
            if (shadowMap.image != VK_NULL_HANDLE) {
                vkDestroySampler(m_Device.logicalDevice, shadowMap.sampler, nullptr);
                vkDestroyImageView(m_Device.logicalDevice, shadowMap.imageView, nullptr);
                vmaDestroyImage(m_Allocator, shadowMap.image, shadowMap.allocation);
            }
        }

        vkDestroyCommandPool(m_Device.logicalDevice, m_CommandPool, nullptr);

        vkDestroyDescriptorPool(m_Device.logicalDevice, m_DescriptorPool, nullptr);


        m_PipelineManager->Shutdown();
        m_PipelineManager.reset();

        for (const auto& imageView: m_SwapChain.swapChainImageViews) {
            vkDestroyImageView(m_Device.logicalDevice, imageView, nullptr);
        }

        vkDestroySwapchainKHR(m_Device.logicalDevice, m_SwapChain.swapChain, nullptr);

        vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);

        vkDestroyImageView(m_Device.logicalDevice, m_DepthImage.imageView, nullptr);
        vmaDestroyImage(m_Allocator, m_DepthImage.image, m_DepthImage.allocation);

        vkDestroyDescriptorSetLayout(m_Device.logicalDevice, m_FrameDescriptorLayout, nullptr);
        vkDestroyDescriptorSetLayout(m_Device.logicalDevice, m_MaterialDescriptorLayout, nullptr);

        vmaDestroyAllocator(m_Allocator);

        vkDestroyDevice(m_Device.logicalDevice, nullptr);
#ifndef NDEBUG
        const auto vkDestroyDebugUtilsMessengerEXT = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(
            m_Instance, "vkDestroyDebugUtilsMessengerEXT"));
        if (!vkDestroyDebugUtilsMessengerEXT)
            OSIRIS_ERROR("Failed to Destroy Debug Messenger");
        else
            vkDestroyDebugUtilsMessengerEXT(m_Instance, m_DebugMessenger, nullptr);
#endif
        vkDestroyInstance(m_Instance, nullptr);
    }

    void VulkanRHI::BeginFrame() {
        vkWaitForFences(m_Device.logicalDevice, 1, &m_Frames[m_CurrentFrame].inFlightFence, VK_TRUE, UINT64_MAX);

        const VkResult acquireResult = vkAcquireNextImageKHR(m_Device.logicalDevice, m_SwapChain.swapChain, UINT64_MAX,
            m_ImageAvailableSemaphores[m_CurrentFrame], VK_NULL_HANDLE, &m_ImageIndex);

        if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
            RecreateSwapChain();
            return;
        } else if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
            OSIRIS_ERROR("Failed to acquire swap chain image!");
            return;
        }

        vkResetFences(m_Device.logicalDevice, 1, &m_Frames[m_CurrentFrame].inFlightFence);
        vkResetCommandBuffer(m_Frames[m_CurrentFrame].commandBuffer, 0);

        constexpr VkCommandBufferBeginInfo beginInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        };
        vkBeginCommandBuffer(m_Frames[m_CurrentFrame].commandBuffer, &beginInfo);

        m_FrameStarted = true;
    }

    void VulkanRHI::EndFrame() {
        if (!m_FrameStarted) return;
        m_FrameStarted = false;

        VkCommandBuffer cmd = m_Frames[m_CurrentFrame].commandBuffer;

        vkCmdEndRendering(cmd);

        // Use render graph for present barrier
        m_RenderGraph.Reset();
        m_RenderGraph.ImportTexture(m_ColorBufferRG,
            m_SwapChain.swapChainImages[m_ImageIndex], ResourceState::ColorWrite);

        m_RenderGraph.AddPass("PresentPass", PassType::Graphics)
            .Read({m_ColorBufferRG, ResourceState::Present})
            .SetExecute(nullptr);

        m_RenderGraph.Compile();
        m_RenderGraph.Execute(cmd);

        vkEndCommandBuffer(cmd);

        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submitInfo = {
            .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .waitSemaphoreCount   = 1,
            .pWaitSemaphores      = &m_ImageAvailableSemaphores[m_CurrentFrame],
            .pWaitDstStageMask    = &waitStage,
            .commandBufferCount   = 1,
            .pCommandBuffers      = &cmd,
            .signalSemaphoreCount = 1,
            .pSignalSemaphores    = &m_RenderFinishedSemaphores.at(m_ImageIndex),
        };
        vkQueueSubmit(m_Device.graphicsQueue, 1, &submitInfo, m_Frames[m_CurrentFrame].inFlightFence);
    }

    void VulkanRHI::Present() {
        VkPresentInfoKHR presentInfo{
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &m_RenderFinishedSemaphores.at(m_ImageIndex),
            .swapchainCount = 1,
            .pSwapchains = &m_SwapChain.swapChain,
            .pImageIndices = &m_ImageIndex,
        };

        const VkResult result = vkQueuePresentKHR(m_Device.graphicsQueue, &presentInfo);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
            RecreateSwapChain();
        }else if (result != VK_SUCCESS) {
            OSIRIS_ERROR("Failed to present");
        }
        m_CurrentFrame = (m_CurrentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    }

    void VulkanRHI::UploadBufferData(BufferHandle handle, const void *data, uint64_t size) {
        BufferDesc stagingDesc = {
            .size       = size,
            .usage      = BufferUsage::Transfer,
            .cpuVisible = true,
        };
        BufferHandle stagingHandle = CreateBuffer(stagingDesc);
        VulkanBuffer& staging = m_Buffers[stagingHandle.id];

        memcpy(staging.allocationInfo.pMappedData, data, size);

        VkCommandBuffer cmd = BeginOneTimeCommands();

        VkBufferCopy copyRegion = {
            .srcOffset = 0,
            .dstOffset = 0,
            .size      = size,
        };
        vkCmdCopyBuffer(cmd, staging.buffer, m_Buffers[handle.id].buffer, 1, &copyRegion);

        EndOneTimeCommands(cmd);

        DestroyBuffer(stagingHandle);
    }
    void VulkanRHI::UploadDynamicBuffer(BufferHandle handle, const void *data, uint64_t size) {
        // TODO: Fix Race condition between CPU and GPU
        memcpy(m_Buffers[handle.id].allocationInfo.pMappedData, data, size);
    }

    void VulkanRHI::SetMeshData(const Mesh &mesh) {
        m_BoundMesh = mesh;
    }

    void VulkanRHI::SetModelMatrix(const glm::mat4 &model) {
        m_ModelMatrix = model;
    }

    BufferHandle VulkanRHI::CreateBuffer(const BufferDesc & desc) {
        VkBufferUsageFlags usage = 0;
        switch (desc.usage) {
            case BufferUsage::Vertex:  usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;  break;
            case BufferUsage::Index:   usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;   break;
            case BufferUsage::Uniform: usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT; break;
            case BufferUsage::Storage: usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT; break;
            case BufferUsage::Transfer: usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT; break;
        }
        VkBufferCreateInfo bufferCreateInfo{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = desc.size,
            .usage = usage | (desc.usage != BufferUsage::Transfer ? VK_BUFFER_USAGE_TRANSFER_DST_BIT : 0),
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };

        VmaAllocationCreateInfo allocationCreateInfo = {
            .usage = desc.cpuVisible ? VMA_MEMORY_USAGE_CPU_TO_GPU : VMA_MEMORY_USAGE_GPU_ONLY,
        };
        allocationCreateInfo.flags = desc.cpuVisible ? VMA_ALLOCATION_CREATE_MAPPED_BIT : 0;

        VulkanBuffer vulkanBuffer;
        vulkanBuffer.size = desc.size;
        const VkResult result = vmaCreateBuffer(m_Allocator, &bufferCreateInfo, &allocationCreateInfo, &vulkanBuffer.buffer, &vulkanBuffer.allocation, &vulkanBuffer.allocationInfo);
        VK_CHECK(result);



        uint32_t bufferIndex = AllocateSlot(m_Buffers, vulkanBuffer, [](const VulkanBuffer& b) {
            return b.buffer == VK_NULL_HANDLE;
        });

        return BufferHandle{ bufferIndex };
    }

    VkCommandBuffer VulkanRHI::BeginOneTimeCommands() {
        VkCommandBufferAllocateInfo allocInfo = {
            .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool        = m_CommandPool,
            .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };
        VkCommandBuffer cmd;
        vkAllocateCommandBuffers(m_Device.logicalDevice, &allocInfo, &cmd);

        VkCommandBufferBeginInfo beginInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        };
        vkBeginCommandBuffer(cmd, &beginInfo);
        return cmd;
    }

    void VulkanRHI::EndOneTimeCommands(VkCommandBuffer cmd) {
        vkEndCommandBuffer(cmd);

        VkSubmitInfo submitInfo = {
            .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .commandBufferCount = 1,
            .pCommandBuffers    = &cmd,
        };
        vkQueueSubmit(m_Device.graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(m_Device.graphicsQueue);
        vkFreeCommandBuffers(m_Device.logicalDevice, m_CommandPool, 1, &cmd);
    }

    void VulkanRHI::TransitionImageLayout(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout) {
        VkImageMemoryBarrier barrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .oldLayout = oldLayout,
            .newLayout = newLayout,
            .image = image,
            .subresourceRange = {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel   = 0,
                .levelCount     = 1,
                .baseArrayLayer = 0,
                .layerCount     = 1,
            },
        };

        VkPipelineStageFlags srcStage, dstStage;

        if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
            barrier.srcAccessMask = VK_ACCESS_NONE;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        } else {
            OSIRIS_ERROR("Unsupported layout transition!");
            return;
        }

        VkCommandBuffer cmd = BeginOneTimeCommands();
        vkCmdPipelineBarrier(cmd, srcStage, dstStage,
            0, 0, nullptr, 0, nullptr, 1, &barrier);
        EndOneTimeCommands(cmd);
    }

    TextureHandle VulkanRHI::CreateTexture(const TextureDesc &desc) {
        // Create image
        VkImageCreateInfo imageCreateInfo = {
            .sType       = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType   = VK_IMAGE_TYPE_2D,
            .format      = VK_FORMAT_R8G8B8A8_UNORM,
            .extent      = {desc.width, desc.height, 1},
            .mipLevels   = 1,
            .arrayLayers = 1,
            .samples     = VK_SAMPLE_COUNT_1_BIT,
            .tiling      = VK_IMAGE_TILING_OPTIMAL,
            .usage       = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        };

        const VmaAllocationCreateInfo allocationCreateInfo = {
            .usage = VMA_MEMORY_USAGE_GPU_ONLY,
        };

        VulkanImage textureImage;
        textureImage.format = VK_FORMAT_R8G8B8A8_UNORM;
        VK_CHECK(vmaCreateImage(m_Allocator, &imageCreateInfo, &allocationCreateInfo,
            &textureImage.image, &textureImage.allocation, nullptr));

        // Transition to transfer destination
        TransitionImageLayout(textureImage.image,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        // Upload via staging buffer
        BufferDesc stagingDesc = {
            .size       = desc.dataSize,
            .usage      = BufferUsage::Transfer,
            .cpuVisible = true,
        };
        BufferHandle stagingHandle = CreateBuffer(stagingDesc);
        memcpy(m_Buffers[stagingHandle.id].allocationInfo.pMappedData, desc.pixels, desc.dataSize);

        VkCommandBuffer cmd = BeginOneTimeCommands();

        VkBufferImageCopy copyRegion = {
            .bufferOffset      = 0,
            .bufferRowLength   = 0,
            .bufferImageHeight = 0,
            .imageSubresource  = {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel       = 0,
                .baseArrayLayer = 0,
                .layerCount     = 1,
            },
            .imageOffset = {0, 0, 0},
            .imageExtent = {desc.width, desc.height, 1},
        };

        vkCmdCopyBufferToImage(cmd,
            m_Buffers[stagingHandle.id].buffer,
            textureImage.image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &copyRegion);

        EndOneTimeCommands(cmd);

        DestroyBuffer(stagingHandle);

        // Transition to shader readable
        TransitionImageLayout(textureImage.image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        // Create image view
        VkImageViewCreateInfo viewCreateInfo = {
            .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image    = textureImage.image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format   = VK_FORMAT_R8G8B8A8_UNORM,
            .subresourceRange = {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel   = 0,
                .levelCount     = 1,
                .baseArrayLayer = 0,
                .layerCount     = 1,
            },
        };
        VK_CHECK(vkCreateImageView(m_Device.logicalDevice, &viewCreateInfo, nullptr, &textureImage.imageView));

        // Create sampler
        VkSamplerCreateInfo samplerInfo = {
            .sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter    = VK_FILTER_LINEAR,
            .minFilter    = VK_FILTER_LINEAR,
            .mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR,
            .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
            .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
            .anisotropyEnable = VK_FALSE,
            .maxAnisotropy    = 1.0f,
            .compareEnable    = VK_FALSE,
            .borderColor      = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
            .unnormalizedCoordinates = VK_FALSE,
        };
        VK_CHECK(vkCreateSampler(m_Device.logicalDevice, &samplerInfo, nullptr, &textureImage.sampler));

        // Store in slot map
        uint32_t index = AllocateSlot(m_Textures, textureImage,
            [](const VulkanImage& t) { return t.image == VK_NULL_HANDLE; });
        return TextureHandle{ index };
    }

    ShaderHandle VulkanRHI::CreateShader(const ShaderDesc& desc) {
        return ShaderHandle();
    }

    MaterialHandle VulkanRHI::CreateMaterial(const MaterialDesc& desc) {
        // 1. Allocate a descriptor set from the pool using the material layout
        VkDescriptorSet materialSet;
        VkDescriptorSetAllocateInfo allocInfo = {
            .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool     = m_DescriptorPool,
            .descriptorSetCount = 1,
            .pSetLayouts        = &m_MaterialDescriptorLayout,
        };
        VK_CHECK(vkAllocateDescriptorSets(m_Device.logicalDevice, &allocInfo, &materialSet));

        // 2. Write the texture into it
        VulkanImage& texture = m_Textures[desc.albedo.id];
        VkDescriptorImageInfo imageInfo = {
            .sampler     = texture.sampler,
            .imageView   = texture.imageView,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };
        VkWriteDescriptorSet write = {
            .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet          = materialSet,
            .dstBinding      = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo      = &imageInfo,
        };
        vkUpdateDescriptorSets(m_Device.logicalDevice, 1, &write, 0, nullptr);

        // 3. Store and return handle
        VulkanMaterial material;
        material.descriptorSet = materialSet;
        uint32_t index = AllocateSlot(m_Materials, material,
        [](const VulkanMaterial& m) { return m.descriptorSet == VK_NULL_HANDLE; });
        return MaterialHandle { index } ;
    }

    void VulkanRHI::DestroyBuffer(BufferHandle handle) {
        if (!handle.IsValid()) return;
        VulkanBuffer& buffer = m_Buffers[handle.id];
        vmaDestroyBuffer(m_Allocator, buffer.buffer, buffer.allocation);
        buffer.buffer     = VK_NULL_HANDLE;
        buffer.allocation = VK_NULL_HANDLE;
        buffer.size       = 0;
    }

    void VulkanRHI::DestroyTexture(TextureHandle handle) {
        if (!handle.IsValid()) return;
        VulkanImage& texture = m_Textures[handle.id];
        vkDestroySampler(m_Device.logicalDevice, texture.sampler, nullptr);
        vkDestroyImageView(m_Device.logicalDevice, texture.imageView, nullptr);
        vmaDestroyImage(m_Allocator, texture.image, texture.allocation);
        texture.image      = VK_NULL_HANDLE;
        texture.allocation = VK_NULL_HANDLE;
        texture.imageView  = VK_NULL_HANDLE;
        texture.sampler    = VK_NULL_HANDLE;
    }

    void VulkanRHI::DestroyShader(ShaderHandle) {
    }

    void VulkanRHI::BindMaterial(MaterialHandle handle) {
        if (!handle.IsValid()) return;
        VkDescriptorSet materialSet = m_Materials[handle.id].descriptorSet;
        vkCmdBindDescriptorSets(
            m_Frames[m_CurrentFrame].commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            m_ForwardPipelineLayout,
            1,        // ← set index 1 (material set)
            1,
            &materialSet,
            0, nullptr
        );
    }

    void VulkanRHI::BindPipeline(PipelineHandle pipeline) {
    }

    void VulkanRHI::Draw(uint32_t vertexCount) {
    }

    void VulkanRHI::DrawIndexed(uint32_t indexCount) {
        VkCommandBuffer cmd = m_Frames[m_CurrentFrame].commandBuffer;

        vkCmdPushConstants(cmd, m_ForwardPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
            0, sizeof(glm::mat4), &m_ModelMatrix);

        if (m_BoundMesh.vertexBuffer.IsValid()) {
            const VkBuffer vertexBuffers[] = { m_Buffers[m_BoundMesh.vertexBuffer.id].buffer };
            constexpr VkDeviceSize offsets[] = { 0 };
            vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
        }

        if (m_BoundMesh.indexBuffer.IsValid()) {
            vkCmdBindIndexBuffer(cmd, m_Buffers[m_BoundMesh.indexBuffer.id].buffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cmd, indexCount, 1, 0, 0, 0);
        } else {
            vkCmdDraw(cmd, m_BoundMesh.vertexCount, 1, 0, 0);
        }
    }

    void VulkanRHI::InitImGui() {
        const VkDescriptorPoolSize pool_sizes[] = { { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 } };

        VkDescriptorPoolCreateInfo pool_info = {};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool_info.maxSets = 1000;
        pool_info.poolSizeCount = static_cast<uint32_t>(std::size(pool_sizes));
        pool_info.pPoolSizes = pool_sizes;


        VK_CHECK(vkCreateDescriptorPool(m_Device.logicalDevice, &pool_info, nullptr, &m_ImGuiDescriptorPool));

        ImGui::CreateContext();

        // this initializes imgui for SDL
        ImGui_ImplSDL2_InitForVulkan(m_Desc.windowHandle);

        // this initializes imgui for Vulkan
        ImGui_ImplVulkan_InitInfo init_info = {};
        init_info.Instance = m_Instance;
        init_info.PhysicalDevice = m_Device.physicalDevice;
        init_info.Device = m_Device.logicalDevice;
        init_info.Queue = m_Device.graphicsQueue;
        init_info.DescriptorPool = m_ImGuiDescriptorPool;
        init_info.MinImageCount = 3;
        init_info.ImageCount = 3;
        init_info.UseDynamicRendering = true;
        init_info.ApiVersion   = VK_API_VERSION_1_4;
        init_info.QueueFamily  = m_Device.graphicsQueueIndex;


        //dynamic rendering parameters for imgui to use
        init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

#ifdef IMGUI_IMPL_VULKAN_HAS_DYNAMIC_RENDERING
        init_info.PipelineInfoMain.PipelineRenderingCreateInfo = {
            .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR,
            .colorAttachmentCount    = 1,
            .pColorAttachmentFormats = &m_SwapChain.swapChainImageFormat,
            .depthAttachmentFormat   = VK_FORMAT_D32_SFLOAT,
        };
#endif

        ImGui_ImplVulkan_Init(&init_info);
    }

    void VulkanRHI::ShutdownImGui() {
        vkDeviceWaitIdle(m_Device.logicalDevice);
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
        vkDestroyDescriptorPool(m_Device.logicalDevice, m_ImGuiDescriptorPool, nullptr);
    }

    void VulkanRHI::BeginImGuiFrame() {
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
    }

    void VulkanRHI::RenderImGui() {
        ImGui::Render();
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(),
                                        m_Frames[m_CurrentFrame].commandBuffer);
    }

    void VulkanRHI::Dispatch(uint32_t x, uint32_t y, uint32_t z) {
    }

    void VulkanRHI::UpdateCamera(const glm::mat4& view, const glm::mat4& projection) {
        struct CameraBufferFull {
            glm::mat4 view;
            glm::mat4 projection;
            glm::mat4 lightSpaceMatrices[3];
            glm::vec4 cascadeSplits;
            glm::vec4 lightDirection;
        };

        CameraBufferFull cameraBuffer;
        cameraBuffer.view       = view;
        cameraBuffer.projection = projection;
        for (uint32_t i = 0; i < SHADOW_CASCADE_COUNT; i++) {
            cameraBuffer.lightSpaceMatrices[i] = m_LightSpaceMatrices[i];
        }
        cameraBuffer.cascadeSplits = glm::vec4(
            m_CascadeSplits[0],
            m_CascadeSplits[1],
            m_CascadeSplits[2],
            0.0f
        );
        cameraBuffer.lightDirection = glm::vec4(-m_DirectionalLight.direction, 0.0f);


        UploadDynamicBuffer(m_CameraUniformBuffer, &cameraBuffer, sizeof(CameraBufferFull));
        UpdateCascades(view, projection);
    }

    void VulkanRHI::BeginForwardPass() {
        if (!m_FrameStarted) return;
        VkCommandBuffer cmd = m_Frames[m_CurrentFrame].commandBuffer;

        m_RenderGraph.Reset();
        m_RenderGraph.ImportTexture(m_ColorBufferRG,
            m_SwapChain.swapChainImages[m_ImageIndex], ResourceState::Undefined);
        m_RenderGraph.ImportTexture(m_DepthBufferRG,
            m_DepthImage.image, ResourceState::Undefined);

        m_RenderGraph.AddPass("ForwardPass", PassType::Graphics)
            .Write({m_ColorBufferRG, ResourceState::ColorWrite})
            .Write({m_DepthBufferRG, ResourceState::DepthWrite})
            .SetExecute(nullptr);

        m_RenderGraph.Compile();
        m_RenderGraph.Execute(cmd);

        VkRenderingAttachmentInfo colorAttachment = {
            .sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView   = m_SwapChain.swapChainImageViews[m_ImageIndex],
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp     = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue  = {.color = {0.0f, 0.0f, 0.0f, 1.0f}},
        };

        VkRenderingAttachmentInfo depthAttachment = {
            .sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView   = m_DepthImage.imageView,
            .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            .loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp     = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .clearValue  = {.depthStencil = {1.0f, 0}},
        };

        const VkRenderingInfo renderingInfo = {
            .sType                = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea           = {.offset = {0, 0}, .extent = m_SwapChain.swapChainExtent},
            .layerCount           = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments    = &colorAttachment,
            .pDepthAttachment     = &depthAttachment,
        };

        vkCmdBeginRendering(cmd, &renderingInfo);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_ForwardPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            m_ForwardPipelineLayout, 0, 1, &m_DescriptorSet, 0, nullptr);

        VkViewport viewport = {
            .x = 0.0f, .y = 0.0f,
            .width    = static_cast<float>(m_SwapChain.swapChainExtent.width),
            .height   = static_cast<float>(m_SwapChain.swapChainExtent.height),
            .minDepth = 0.0f, .maxDepth = 1.0f,
        };
        vkCmdSetViewport(cmd, 0, 1, &viewport);

        VkRect2D scissor = { .offset = {0, 0}, .extent = m_SwapChain.swapChainExtent };
        vkCmdSetScissor(cmd, 0, 1, &scissor);
    }

    void VulkanRHI::BeginShadowPass(uint32_t cascadeIndex) {
    if (!m_FrameStarted) return;
    m_CurrentCascadeIndex = cascadeIndex;
    VkCommandBuffer cmd = m_Frames[m_CurrentFrame].commandBuffer;

    // Transition shadow map UNDEFINED → DEPTH_ATTACHMENT
    VkImageMemoryBarrier barrier = {
        .sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        .oldLayout     = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout     = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .image         = m_ShadowMaps[cascadeIndex].image,
        .subresourceRange = {
            .aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1,
        },
    };
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkRenderingAttachmentInfo depthAttachment = {
        .sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView   = m_ShadowMaps[cascadeIndex].imageView,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp     = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue  = {.depthStencil = {1.0f, 0}},
    };

    VkRenderingInfo renderingInfo = {
        .sType                = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea           = {.offset = {0, 0}, .extent = {SHADOW_MAP_SIZE, SHADOW_MAP_SIZE}},
        .layerCount           = 1,
        .colorAttachmentCount = 0,
        .pDepthAttachment     = &depthAttachment,
    };

    vkCmdBeginRendering(cmd, &renderingInfo);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_ShadowPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        m_ShadowPipelineLayout, 0, 1, &m_DescriptorSet, 0, nullptr);

    VkViewport viewport = {
        .x = 0.0f, .y = 0.0f,
        .width    = static_cast<float>(SHADOW_MAP_SIZE),
        .height   = static_cast<float>(SHADOW_MAP_SIZE),
        .minDepth = 0.0f, .maxDepth = 1.0f,
    };
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor = {
        .offset = {0, 0},
        .extent = {SHADOW_MAP_SIZE, SHADOW_MAP_SIZE},
    };
    vkCmdSetScissor(cmd, 0, 1, &scissor);
}

void VulkanRHI::EndShadowPass(uint32_t cascadeIndex) {
    if (!m_FrameStarted) return;
    VkCommandBuffer cmd = m_Frames[m_CurrentFrame].commandBuffer;
    vkCmdEndRendering(cmd);

    // Transition shadow map DEPTH_ATTACHMENT → SHADER_READ
    VkImageMemoryBarrier barrier = {
        .sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .oldLayout     = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .image         = m_ShadowMaps[cascadeIndex].image,
        .subresourceRange = {
            .aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1,
        },
    };
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
}

    void VulkanRHI::DrawShadowIndexed(uint32_t indexCount) {
        if (!m_FrameStarted) return;
        VkCommandBuffer cmd = m_Frames[m_CurrentFrame].commandBuffer;

        struct ShadowPushConstants {
            glm::mat4 model;
            glm::mat4 lightSpaceMatrix;
        };

        ShadowPushConstants push = {
            .model            = m_ModelMatrix,
            .lightSpaceMatrix = m_LightSpaceMatrices[m_CurrentCascadeIndex],
        };

        vkCmdPushConstants(cmd, m_ShadowPipelineLayout,
            VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ShadowPushConstants), &push);

        if (m_BoundMesh.vertexBuffer.IsValid()) {
            VkBuffer vertexBuffers[] = { m_Buffers[m_BoundMesh.vertexBuffer.id].buffer };
            VkDeviceSize offsets[]   = { 0 };
            vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
        }

        if (m_BoundMesh.indexBuffer.IsValid()) {
            vkCmdBindIndexBuffer(cmd, m_Buffers[m_BoundMesh.indexBuffer.id].buffer,
                0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cmd, indexCount, 1, 0, 0, 0);
        }
    }

bool VulkanRHI::SetupDebugMessenger() {
        VkDebugUtilsMessengerCreateInfoEXT createInfo = {
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
            .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                     VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
            .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT,
        };
        createInfo.pfnUserCallback = DebugCallback;

        const auto vkCreateDebugUtilsMessengerEXT = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(m_Instance, "vkCreateDebugUtilsMessengerEXT"));
        if (!vkCreateDebugUtilsMessengerEXT) {
            OSIRIS_ERROR("Failed to get vkCreateDebugUtilsMessengerEXT!");
            return false;
        }

        VkResult result = vkCreateDebugUtilsMessengerEXT(m_Instance, &createInfo, nullptr, &m_DebugMessenger);
        VK_CHECK(result);
        return true;
    }

    VkBool32 VulkanRHI::DebugCallback(const VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        VkDebugUtilsMessageTypeFlagsEXT type, const VkDebugUtilsMessengerCallbackDataEXT *callbackData,
        void *userData) {
        if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
            OSIRIS_ERROR("Vulkan: {}", callbackData->pMessage);
        else if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
            OSIRIS_WARN("Vulkan: {}", callbackData->pMessage);
        else
            OSIRIS_INFO("Vulkan: {}", callbackData->pMessage);

        return VK_FALSE;
    }

    bool VulkanRHI::SelectPhysicalDevice() {
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(m_Instance, &deviceCount, nullptr);
        if (deviceCount == 0) {
            OSIRIS_ERROR("Failed to get physical device count!");
            return false;
        }

        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(m_Instance, &deviceCount, devices.data());
        for (const auto& device : devices) {
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(device, &props);
            if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                m_Device.physicalDevice = device;
                return true;
            }
        }
        OSIRIS_WARN("Using Integrated GPU");
        m_Device.physicalDevice = devices[0];
        return true;
    }

    bool VulkanRHI::CreateLogicalDevice() {
        uint32_t queueCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(m_Device.physicalDevice, &queueCount, nullptr);
        if (queueCount == 0) {
            OSIRIS_ERROR("Failed to get queue families!");
            return false;
        }
        std::vector<VkQueueFamilyProperties> queueFamilies(queueCount);
        vkGetPhysicalDeviceQueueFamilyProperties(m_Device.physicalDevice, &queueCount, queueFamilies.data());

        for (uint32_t i = 0; i < queueCount; i++) {
            if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                m_Device.graphicsQueueIndex = i;
            }
            else if (queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
                m_Device.computeQueueIndex = i;
            }
            else if (queueFamilies[i].queueFlags & VK_QUEUE_TRANSFER_BIT) {
                m_Device.transferQueueIndex = i;
            }
        }

        float queuePriority = 1.0f;
        VkDeviceQueueCreateInfo queueCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = m_Device.graphicsQueueIndex,
            .queueCount = 1,
            .pQueuePriorities = &queuePriority,
        };

        VkPhysicalDeviceFeatures physicalDeviceFeatures = {};
        const char* deviceExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        VkPhysicalDeviceVulkan13Features features13 = {
            .sType          = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
            .dynamicRendering = VK_TRUE,
        };

        const VkDeviceCreateInfo deviceCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            .pNext = &features13,
            .queueCreateInfoCount = 1,
            .pQueueCreateInfos = &queueCreateInfo,
            .enabledExtensionCount = 1,
            .ppEnabledExtensionNames = deviceExtensions,
            .pEnabledFeatures = &physicalDeviceFeatures
        };

        VkResult result = vkCreateDevice(m_Device.physicalDevice, &deviceCreateInfo, nullptr, &m_Device.logicalDevice);
        VK_CHECK(result);

        vkGetDeviceQueue(m_Device.logicalDevice, m_Device.graphicsQueueIndex, 0, &m_Device.graphicsQueue);
        // vkGetDeviceQueue(m_Device.logicalDevice, m_Device.computeQueueIndex, 0, &m_Device.computeQueue);
        // vkGetDeviceQueue(m_Device.logicalDevice, m_Device.transferQueueIndex, 0, &m_Device.transferQueue);

        VmaAllocatorCreateInfo allocatorInfo = {
            .physicalDevice = m_Device.physicalDevice,
            .device         = m_Device.logicalDevice,
            .instance       = m_Instance,
        };
        result = vmaCreateAllocator(&allocatorInfo, &m_Allocator);
        VK_CHECK(result);
        return true;
    }

    bool VulkanRHI::CreateSurface() {
        if (!SDL_Vulkan_CreateSurface(m_Desc.windowHandle, m_Instance, &m_Surface)) {
            OSIRIS_ERROR("Failed to create Vulkan surface!");
            return false;
        }
        return true;
    }

    bool VulkanRHI::CreateSwapChain() {
        VkSurfaceCapabilitiesKHR surfaceCapabilities;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_Device.physicalDevice, m_Surface, &surfaceCapabilities);

        uint32_t formatCount;
        vkGetPhysicalDeviceSurfaceFormatsKHR(m_Device.physicalDevice, m_Surface, &formatCount, nullptr);
        if (formatCount == 0) {
            OSIRIS_ERROR("Failed to get surface formats!");
            return false;
        }

        std::vector<VkSurfaceFormatKHR> formats(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(m_Device.physicalDevice, m_Surface, &formatCount, formats.data());

        VkSurfaceFormatKHR surfaceFormat = formats.at(0);
        for (const auto& format : formats) {
            if (
                format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR
                &&
                format.format == VK_FORMAT_B8G8R8A8_SRGB
                ) {
                surfaceFormat = format;
            }
        }
        m_SwapChain.swapChainImageFormat = surfaceFormat.format;

        uint32_t presentModeCount;
        vkGetPhysicalDeviceSurfacePresentModesKHR(m_Device.physicalDevice, m_Surface, &presentModeCount, nullptr);
        if (presentModeCount == 0) {
            OSIRIS_ERROR("Failed to get surface present modes!");
            return false;
        }
        std::vector<VkPresentModeKHR> presentModes(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(m_Device.physicalDevice, m_Surface, &presentModeCount, presentModes.data());

        VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
        for (const auto& mode : presentModes) {
            if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
                presentMode = mode;
                break;
            }
        }

        m_SwapChain.swapChainExtent = surfaceCapabilities.currentExtent;

        VkSwapchainCreateInfoKHR swapChainCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
            .surface = m_Surface,
            .minImageCount = surfaceCapabilities.minImageCount + 1,
            .imageFormat = surfaceFormat.format,
            .imageColorSpace = surfaceFormat.colorSpace,
            .imageExtent = m_SwapChain.swapChainExtent,
            .imageArrayLayers = 1,
            .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            .preTransform = surfaceCapabilities.currentTransform,
            .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
            .presentMode = presentMode,
            .clipped = VK_TRUE
        };

        VkResult result = vkCreateSwapchainKHR(m_Device.logicalDevice, &swapChainCreateInfo, nullptr, &m_SwapChain.swapChain);
        VK_CHECK(result);

        return true;

    }

    bool VulkanRHI::CreateSwapChainImages() {
        uint32_t imageCount;
        vkGetSwapchainImagesKHR(m_Device.logicalDevice, m_SwapChain.swapChain, &imageCount, nullptr);
        if (imageCount == 0) {
            OSIRIS_ERROR("Failed to get surface images!");
            return false;
        }
        m_SwapChain.swapChainImages.resize(imageCount);
        vkGetSwapchainImagesKHR(m_Device.logicalDevice, m_SwapChain.swapChain, &imageCount, m_SwapChain.swapChainImages.data());
        m_RenderFinishedSemaphores.resize(m_SwapChain.swapChainImages.size());

        VkSemaphoreCreateInfo semaphoreInfo = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
        };

        for (auto& semaphore : m_RenderFinishedSemaphores) {
            VkResult result = vkCreateSemaphore(
                m_Device.logicalDevice,
                &semaphoreInfo,
                nullptr,
                &semaphore
            );
            VK_CHECK(result);
        }

        m_SwapChain.swapChainImageViews.resize(imageCount);
        for (uint32_t i = 0; i < imageCount; i++) {
            VkImageViewCreateInfo imageViewCreateInfo = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image = m_SwapChain.swapChainImages[i],
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = m_SwapChain.swapChainImageFormat,

                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer= 0,
                    .layerCount = 1
                }
            };
            const VkResult result = vkCreateImageView(m_Device.logicalDevice, &imageViewCreateInfo, nullptr, &m_SwapChain.swapChainImageViews[i]);
            VK_CHECK(result);
        }

        return true;
    }

    bool VulkanRHI::CreateDepthResources() {
        VkImageCreateInfo imageCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = VK_FORMAT_D32_SFLOAT,
            .extent = {m_SwapChain.swapChainExtent.width, m_SwapChain.swapChainExtent.height, 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        };

        m_DepthImage.format = VK_FORMAT_D32_SFLOAT;

        VmaAllocationCreateInfo allocationCreateInfo = {
            .usage = VMA_MEMORY_USAGE_GPU_ONLY,
        };

        VK_CHECK(vmaCreateImage(m_Allocator, &imageCreateInfo, &allocationCreateInfo, &m_DepthImage.image, &m_DepthImage.allocation, nullptr));

        VkImageViewCreateInfo viewCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = m_DepthImage.image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = VK_FORMAT_D32_SFLOAT,

            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer= 0,
                .layerCount = 1
            }
        };
        const VkResult result = vkCreateImageView(m_Device.logicalDevice, &viewCreateInfo, nullptr, &m_DepthImage.imageView);
        if (result != VK_SUCCESS) {
            OSIRIS_ERROR("Failed to create image view for depth buffer!");
            return false;
        }

        return true;
    }

    std::vector<char> readFileBinary(const std::string& filePath) {
        std::ifstream file(filePath, std::ios::binary | std::ios::ate);
        if (!file) {
            OSIRIS_ERROR("Failed to open file!");
            return {};
        }

        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);

        std::vector<char> buffer(size);

        if (!file.read(buffer.data(), size)) {
            OSIRIS_ERROR("Failed to read file: {}", filePath);
        }

        return buffer;
    }

    VkShaderModule VulkanRHI::LoadShader(const std::string &shaderPath) {
        std::vector<char> buffer(readFileBinary(shaderPath));
        if (buffer.empty()) {
            OSIRIS_ERROR("Failed to shader: {}", shaderPath);
            return VK_NULL_HANDLE;
        }

        VkShaderModuleCreateInfo createInfo = {
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = buffer.size(),
            .pCode = reinterpret_cast<uint32_t*>(buffer.data())
        };

        VkShaderModule module = VK_NULL_HANDLE;
        const VkResult result = vkCreateShaderModule(m_Device.logicalDevice, &createInfo, nullptr, &module);
        VK_CHECK(result);

        return module;
    }

    bool VulkanRHI::CreatePipeline() {
        VkShaderModule vertShaderModule = LoadShader("assets/shaders/triangle.vert.spv");
        VkShaderModule fragShaderModule = LoadShader("assets/shaders/triangle.frag.spv");

        if (!vertShaderModule || !fragShaderModule) {
            return false;
        }

        VkPipelineShaderStageCreateInfo vertStage = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vertShaderModule,
            .pName = "main",
        };

        VkPipelineShaderStageCreateInfo fragStage = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = fragShaderModule,
            .pName = "main",
        };

        VkPipelineShaderStageCreateInfo stages[] = {vertStage, fragStage};

        VkVertexInputBindingDescription vertexDescription = {
            .binding = 0,
            .stride = sizeof(Vertex),
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
        };
        VkVertexInputAttributeDescription vertexAttributeDescriptionPosition = {
            .location = 0,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(Vertex, Position),
        };

        VkVertexInputAttributeDescription vertexAttributeDescriptionNormal = {
            .location = 1,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(Vertex, Normal),
        };

        VkVertexInputAttributeDescription vertexAttributeDescriptionTexCoords = {
            .location = 2,
            .binding = 0,
            .format = VK_FORMAT_R32G32_SFLOAT,
            .offset = offsetof(Vertex, TexCoord),
        };

        VkVertexInputAttributeDescription attributes[] = {
            vertexAttributeDescriptionPosition,
            vertexAttributeDescriptionNormal,
            vertexAttributeDescriptionTexCoords,
        };

        VkPipelineVertexInputStateCreateInfo vertexInput = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .vertexBindingDescriptionCount = 1,
            .pVertexBindingDescriptions = &vertexDescription,
            .vertexAttributeDescriptionCount = 3,
            .pVertexAttributeDescriptions = attributes,
        };



        VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            .primitiveRestartEnable = VK_FALSE,
        };

        VkPipelineViewportStateCreateInfo viewportInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .viewportCount = 1,
            .scissorCount = 1,
        };

        VkPipelineRasterizationStateCreateInfo rasterizationInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .depthClampEnable = VK_FALSE,
            .rasterizerDiscardEnable = VK_FALSE,
            .polygonMode = VK_POLYGON_MODE_FILL,
            .cullMode = VK_CULL_MODE_BACK_BIT,
            .frontFace = VK_FRONT_FACE_CLOCKWISE,
            .depthBiasEnable = VK_FALSE,
            .lineWidth = 1.0f,
        };

        VkPipelineMultisampleStateCreateInfo multisampleInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
            .sampleShadingEnable = VK_FALSE,
        };

        VkPipelineColorBlendAttachmentState colorBlendAttachment = {
            .blendEnable = VK_FALSE,
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
        };

        VkPipelineColorBlendStateCreateInfo colorBlendInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .logicOpEnable = VK_FALSE,
            .attachmentCount = 1,
            .pAttachments = &colorBlendAttachment,
        };

        VkDynamicState dynamicState[] = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
        };

        VkPipelineDynamicStateCreateInfo dynamicStateInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .dynamicStateCount = 2,
            .pDynamicStates = dynamicState,
        };

        VkDescriptorSetLayoutBinding cameraBinding = {
            .binding         = 0,
            .descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 1,
            .stageFlags      = VK_SHADER_STAGE_VERTEX_BIT,
        };
        VkDescriptorSetLayoutCreateInfo frameLayoutInfo = {
            .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = 1,
            .pBindings    = &cameraBinding,
        };
        VK_CHECK(vkCreateDescriptorSetLayout(m_Device.logicalDevice, &frameLayoutInfo, nullptr, &m_FrameDescriptorLayout));

        VkDescriptorSetLayoutBinding textureBinding = {
            .binding         = 0,
            .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT,
        };
        VkDescriptorSetLayoutCreateInfo materialLayoutInfo = {
            .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = 1,
            .pBindings    = &textureBinding,
        };
        VK_CHECK(vkCreateDescriptorSetLayout(m_Device.logicalDevice, &materialLayoutInfo, nullptr, &m_MaterialDescriptorLayout));

        VkDescriptorSetLayout layouts[] = { m_FrameDescriptorLayout, m_MaterialDescriptorLayout };


        VkPushConstantRange pushConstantRange = {
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .offset     = 0,
            .size       = sizeof(glm::mat4),
        };

        VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 2,
            .pSetLayouts = layouts,
            .pushConstantRangeCount = 1,
            .pPushConstantRanges    = &pushConstantRange,
        };

        VkResult result = vkCreatePipelineLayout(m_Device.logicalDevice, &pipelineLayoutInfo, nullptr, &m_ForwardPipelineLayout);
        VK_CHECK(result);

        VkPipelineDepthStencilStateCreateInfo depthStencilInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .depthTestEnable = VK_TRUE,
            .depthWriteEnable = VK_TRUE,
            .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
        };

        VkPipelineRenderingCreateInfo pipelineRenderingInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
            .colorAttachmentCount = 1,
            .pColorAttachmentFormats = &m_SwapChain.swapChainImageFormat,
            .depthAttachmentFormat = VK_FORMAT_D32_SFLOAT,
        };

        VkGraphicsPipelineCreateInfo pipelineCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .pNext = &pipelineRenderingInfo,
            .stageCount = 2,
            .pStages = stages,
            .pVertexInputState = &vertexInput,
            .pInputAssemblyState = &inputAssemblyInfo,
            .pViewportState = &viewportInfo,
            .pRasterizationState = &rasterizationInfo,
            .pMultisampleState = &multisampleInfo,
            .pDepthStencilState = &depthStencilInfo,
            .pColorBlendState = &colorBlendInfo,
            .pDynamicState = &dynamicStateInfo,
            .layout = m_ForwardPipelineLayout,
        };

        result = vkCreateGraphicsPipelines(m_Device.logicalDevice, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &m_ForwardPipeline);
        VK_CHECK(result);
        vkDestroyShaderModule(m_Device.logicalDevice, vertShaderModule, nullptr);
        vkDestroyShaderModule(m_Device.logicalDevice, fragShaderModule, nullptr);

        return true;
    }

    bool VulkanRHI::CreateDescriptorSetLayouts() {
        VkDescriptorSetLayoutBinding frameBindings[] = {
            {
                .binding         = 0,
                .descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount = 1,
                .stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            },
            {
                .binding         = 1,
                .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = 1,
                .stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT,
            },
            {
                .binding         = 2,
                .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = 1,
                .stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT,
            },
            {
                .binding         = 3,
                .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = 1,
                .stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT,
            },
        };

        VkDescriptorSetLayoutCreateInfo frameLayoutInfo = {
            .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = 4,
            .pBindings    = frameBindings,
        };
        VK_CHECK(vkCreateDescriptorSetLayout(m_Device.logicalDevice, &frameLayoutInfo, nullptr, &m_FrameDescriptorLayout));

        VkDescriptorSetLayoutBinding textureBinding = {
            .binding         = 0,
            .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT,
        };
        VkDescriptorSetLayoutCreateInfo materialLayoutInfo = {
            .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = 1,
            .pBindings    = &textureBinding,
        };
        VK_CHECK(vkCreateDescriptorSetLayout(m_Device.logicalDevice, &materialLayoutInfo, nullptr, &m_MaterialDescriptorLayout));

        OSIRIS_INFO("Descriptor set layouts created!");
        return true;
    }

    bool VulkanRHI::CreateDescriptorPool() {
        VkDescriptorPoolSize poolSizes[] = {
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         1 },
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1003  },
        };
        const VkDescriptorPoolCreateInfo descriptorPoolInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = 1001,
            .poolSizeCount = 2,
            .pPoolSizes = poolSizes,
        };

        const VkResult result = vkCreateDescriptorPool(m_Device.logicalDevice, &descriptorPoolInfo, nullptr, &m_DescriptorPool);
        if (result != VK_SUCCESS) {
            return false;
        }
        return true;
    }

    bool VulkanRHI::CreateDescriptorSet() {
        VkDescriptorSetAllocateInfo descriptorSetAllocateInfo = {
            .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool     = m_DescriptorPool,
            .descriptorSetCount = 1,
            .pSetLayouts        = &m_FrameDescriptorLayout,
        };

        VkResult result = vkAllocateDescriptorSets(m_Device.logicalDevice, &descriptorSetAllocateInfo, &m_DescriptorSet);
        if (result != VK_SUCCESS) {
            OSIRIS_ERROR("Failed to allocate descriptor set!");
            return false;
        }

        struct CameraBufferFull {
            glm::mat4 view;
            glm::mat4 projection;
            glm::mat4 lightSpaceMatrices[3];
            glm::vec4 cascadeSplits;
            glm::vec4 lightDirection;
        };

        BufferDesc uniformBufferDesc = {
            .size       = sizeof(CameraBufferFull),
            .usage      = BufferUsage::Uniform,
            .cpuVisible = true,
        };

        m_CameraUniformBuffer = CreateBuffer(uniformBufferDesc);

        VkDescriptorBufferInfo bufferInfo = {
            .buffer = m_Buffers.at(m_CameraUniformBuffer.id).buffer,
            .offset = 0,
            .range  = sizeof(CameraBufferFull),
        };

        const VkWriteDescriptorSet writeDescriptorSet = {
            .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet          = m_DescriptorSet,
            .dstBinding      = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .pBufferInfo     = &bufferInfo,
        };

        vkUpdateDescriptorSets(m_Device.logicalDevice, 1, &writeDescriptorSet, 0, nullptr);

        return true;
    }

    bool VulkanRHI::CreateShadowMap() {
        VkImageCreateInfo imageCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = VK_FORMAT_D32_SFLOAT,
            .extent = {SHADOW_MAP_SIZE, SHADOW_MAP_SIZE, 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .usage     = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, // ← here
        };

        VmaAllocationCreateInfo allocationCreateInfo = {
            .usage = VMA_MEMORY_USAGE_GPU_ONLY,
        };

        for (auto& map : m_ShadowMaps) {
            map.format = VK_FORMAT_D32_SFLOAT;
            VK_CHECK(vmaCreateImage(m_Allocator, &imageCreateInfo, &allocationCreateInfo, &map.image, &map.allocation, nullptr));

            VkImageViewCreateInfo viewCreateInfo = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image = map.image,
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = VK_FORMAT_D32_SFLOAT,

                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer= 0,
                    .layerCount = 1
                }
            };
            const VkResult result = vkCreateImageView(m_Device.logicalDevice, &viewCreateInfo, nullptr, &map.imageView);
            if (result != VK_SUCCESS) {
                OSIRIS_ERROR("Failed to create image view for depth buffer!");
                return false;
            }

            VkSamplerCreateInfo samplerInfo = {
                .sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                .magFilter    = VK_FILTER_LINEAR,
                .minFilter    = VK_FILTER_LINEAR,
                .mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR,
                .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
                .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
                .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
                .compareEnable = VK_TRUE,
                .compareOp     = VK_COMPARE_OP_LESS_OR_EQUAL,
                .borderColor  = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
            };
            VK_CHECK(vkCreateSampler(m_Device.logicalDevice, &samplerInfo, nullptr, &map.sampler));
        }

        return true;
    }


    bool VulkanRHI::CreateCommandBuffers() {
        VkCommandPoolCreateInfo poolInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = m_Device.graphicsQueueIndex
        };

        VkResult result = vkCreateCommandPool(m_Device.logicalDevice, &poolInfo, nullptr, &m_CommandPool);
        if (result != VK_SUCCESS) {
            OSIRIS_ERROR("Could not create command pool");
            return false;
        }

        VkCommandBufferAllocateInfo commandBufferAllocateInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = m_CommandPool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };

        VkSemaphoreCreateInfo semaphoreInfo = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        VkFenceCreateInfo fenceInfo = {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT
        };

        for (auto& frame : m_Frames) {
            result = vkAllocateCommandBuffers(m_Device.logicalDevice, &commandBufferAllocateInfo, &frame.commandBuffer);
            VK_CHECK(result);
            result = vkCreateFence(m_Device.logicalDevice, &fenceInfo, nullptr, &frame.inFlightFence);
            VK_CHECK(result);
        }

        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
                result = vkCreateSemaphore(
                m_Device.logicalDevice,
                &semaphoreInfo,
                nullptr,
                &m_ImageAvailableSemaphores[i]
            );
            VK_CHECK(result);
        }

        return true;
    }


    void VulkanRHI::RecreateSwapChain() {
        vkDeviceWaitIdle(m_Device.logicalDevice);

        for (const auto semaphore : m_RenderFinishedSemaphores) {
            vkDestroySemaphore(m_Device.logicalDevice, semaphore, nullptr);
        }
        m_RenderFinishedSemaphores.clear();

        for (const auto imageView : m_SwapChain.swapChainImageViews) {
            vkDestroyImageView(m_Device.logicalDevice, imageView, nullptr);
        }
        m_SwapChain.swapChainImageViews.clear();
        m_SwapChain.swapChainImages.clear();

        vkDestroyImageView(m_Device.logicalDevice, m_DepthImage.imageView, nullptr);
        m_DepthImage.imageView = VK_NULL_HANDLE;
        vmaDestroyImage(m_Allocator, m_DepthImage.image, m_DepthImage.allocation);
        m_DepthImage.image = VK_NULL_HANDLE;
        m_DepthImage.allocation = VK_NULL_HANDLE;

        vkDestroySwapchainKHR(m_Device.logicalDevice, m_SwapChain.swapChain, nullptr);

        if  (!CreateSwapChain()) {
            OSIRIS_ERROR("Failed to create swap chain on resize!");
            return;
        }
        if (!CreateSwapChainImages()) {
            OSIRIS_ERROR("Failed to create swap chain images on resize!");
            return;
        }
        if (!CreateDepthResources()) {
            OSIRIS_ERROR("Failed to create depth resources on resize!");
            return;
        }
    }

void VulkanRHI::UpdateCascades(const glm::mat4& view, const glm::mat4& projection) {
    // Cascade split distances in view space
    float nearClip  = 1.0f;
    float farClip   = 50.0f;
    float clipRange = farClip - nearClip;

    float cascadeSplitLambda = 0.95f;

    float cascadeSplits[SHADOW_CASCADE_COUNT];

    // Calculate split depths based on view camera frustum
    for (uint32_t i = 0; i < SHADOW_CASCADE_COUNT; i++) {
        float p       = (i + 1) / static_cast<float>(SHADOW_CASCADE_COUNT);
        float log     = nearClip * std::pow(farClip / nearClip, p);
        float uniform = nearClip + clipRange * p;
        float d       = cascadeSplitLambda * (log - uniform) + uniform;
        cascadeSplits[i] = (d - nearClip) / clipRange;
    }

    // Calculate orthographic projection matrix for each cascade
    float lastSplitDist = 0.0f;

    for (uint32_t cascadeIndex = 0; cascadeIndex < SHADOW_CASCADE_COUNT; cascadeIndex++) {
        float splitDist = cascadeSplits[cascadeIndex];

        // Get frustum corners in NDC space
        glm::vec3 frustumCorners[8] = {
            glm::vec3(-1.0f,  1.0f, 0.0f),
            glm::vec3( 1.0f,  1.0f, 0.0f),
            glm::vec3( 1.0f, -1.0f, 0.0f),
            glm::vec3(-1.0f, -1.0f, 0.0f),
            glm::vec3(-1.0f,  1.0f, 1.0f),
            glm::vec3( 1.0f,  1.0f, 1.0f),
            glm::vec3( 1.0f, -1.0f, 1.0f),
            glm::vec3(-1.0f, -1.0f, 1.0f),
        };

        // Transform frustum corners to world space
        glm::mat4 invViewProj = glm::inverse(projection * view);
        for (auto& corner : frustumCorners) {
            glm::vec4 invCorner = invViewProj * glm::vec4(corner, 1.0f);
            corner = glm::vec3(invCorner / invCorner.w);
        }

        // Adjust corners to this cascade's near/far split
        for (uint32_t i = 0; i < 4; i++) {
            glm::vec3 dist        = frustumCorners[i + 4] - frustumCorners[i];
            frustumCorners[i + 4] = frustumCorners[i] + (dist * splitDist);
            frustumCorners[i]     = frustumCorners[i] + (dist * lastSplitDist);
        }

        // Find frustum center
        glm::vec3 frustumCenter = glm::vec3(0.0f);
        for (auto& corner : frustumCorners) {
            frustumCenter += corner;
        }
        frustumCenter /= 8.0f;

        // Find the radius of the cascade sphere
        float radius = 0.0f;
        for (auto& corner : frustumCorners) {
            float distance = glm::length(corner - frustumCenter);
            radius = glm::max(radius, distance);
        }
        radius = std::ceil(radius * 16.0f) / 16.0f;

        glm::vec3 maxExtents = glm::vec3(radius);
        glm::vec3 minExtents = -maxExtents;

        // Build light view matrix
        glm::vec3 lightDir = glm::normalize(-m_DirectionalLight.direction);
        glm::vec3 up       = glm::abs(glm::dot(lightDir, glm::vec3(0,1,0))) < 0.99f
                             ? glm::vec3(0,1,0) : glm::vec3(1,0,0);

        glm::mat4 lightView = glm::lookAt(
            frustumCenter - lightDir * -minExtents.z,
            frustumCenter,
            up
        );

        // Build orthographic projection matrix
        glm::mat4 lightProj = glm::ortho(
            minExtents.x, maxExtents.x,
            minExtents.y, maxExtents.y,
            0.0f, maxExtents.z - minExtents.z
        );

        m_CascadeSplits[cascadeIndex]       = (nearClip + splitDist * clipRange) * -1.0f;
        m_LightSpaceMatrices[cascadeIndex]  = lightProj * lightView;

        lastSplitDist = cascadeSplits[cascadeIndex];
    }
}

    void VulkanRHI::UpdateShadowDescriptors() {
        for (uint32_t i = 0; i < SHADOW_CASCADE_COUNT; i++) {
            VkDescriptorImageInfo imageInfo = {
                .sampler     = m_ShadowMaps[i].sampler,
                .imageView   = m_ShadowMaps[i].imageView,
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            };
            VkWriteDescriptorSet write = {
                .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet          = m_DescriptorSet,
                .dstBinding      = i + 1,  // bindings 1, 2, 3
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo      = &imageInfo,
            };
            vkUpdateDescriptorSets(m_Device.logicalDevice, 1, &write, 0, nullptr);
        }
        OSIRIS_INFO("Shadow descriptors updated!");
    }
} // Osiris