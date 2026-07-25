//
// Created by ahtal on 01/06/2026.
//
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
#include "VulkanRHI.h"

#include <fstream>
#include <SDL_vulkan.h>

#include "VulkanUtils.h"
#include "core/Log.h"
#include "renderer/MeshType.h"


namespace Osiris {
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

        if (!CreatePipeline()) {
            OSIRIS_ERROR("Failed to create pipeline!");
            return false;
        }

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

        return true;
    }

    void VulkanRHI::Shutdown() {
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

        for (auto& texture : m_Textures) {
            vkDestroySampler(m_Device.logicalDevice, texture.sampler, nullptr);
            vkDestroyImageView(m_Device.logicalDevice, texture.imageView, nullptr);
            vmaDestroyImage(m_Allocator, texture.image, texture.allocation);
        }

        vkDestroyCommandPool(m_Device.logicalDevice, m_CommandPool, nullptr);

        vkDestroyPipeline(m_Device.logicalDevice, m_GraphicsPipeline, nullptr);

        vkDestroyDescriptorPool(m_Device.logicalDevice, m_DescriptorPool, nullptr);

        vkDestroyPipelineLayout(m_Device.logicalDevice, m_PipelineLayout, nullptr);

        for (const auto& imageView: m_SwapChain.swapChainImageViews) {
            vkDestroyImageView(m_Device.logicalDevice, imageView, nullptr);
        }

        vkDestroySwapchainKHR(m_Device.logicalDevice, m_SwapChain.swapChain, nullptr);

        vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);

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
        vkResetFences(m_Device.logicalDevice, 1, &m_Frames[m_CurrentFrame].inFlightFence);

        const VkResult result = vkAcquireNextImageKHR(m_Device.logicalDevice, m_SwapChain.swapChain, UINT64_MAX, m_ImageAvailableSemaphores[m_CurrentFrame], VK_NULL_HANDLE, &m_ImageIndex);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
            RecreateSwapChain();
        } else if (result != VK_SUCCESS) {
            OSIRIS_ERROR("Failed to acquire swap chain image!");
        }

        vkResetCommandBuffer(m_Frames.at(m_CurrentFrame).commandBuffer, 0);
        RecordCommandBuffer(m_Frames.at(m_CurrentFrame).commandBuffer, m_ImageIndex);
    }

    void VulkanRHI::EndFrame() {
        const VkCommandBuffer commandBuffer = m_Frames.at(m_CurrentFrame).commandBuffer;

        vkCmdEndRendering(commandBuffer);

        VkImageMemoryBarrier presentBarrier = {
            .sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_NONE,
            .oldLayout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .newLayout     = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .image         = m_SwapChain.swapChainImages[m_ImageIndex],
            .subresourceRange = {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel   = 0,
                .levelCount     = 1,
                .baseArrayLayer = 0,
                .layerCount     = 1
            },
        };

        vkCmdPipelineBarrier(commandBuffer,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            0, 0, nullptr, 0, nullptr, 1, &presentBarrier);

        vkEndCommandBuffer(commandBuffer);

        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submitInfo{
            .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .waitSemaphoreCount   = 1,
            .pWaitSemaphores      = &m_ImageAvailableSemaphores[m_CurrentFrame],
            .pWaitDstStageMask    = &waitStage,
            .commandBufferCount   = 1,
            .pCommandBuffers      = &m_Frames[m_CurrentFrame].commandBuffer,
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



        uint32_t bufferIndex = AllocateBufferSlot(vulkanBuffer);

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
        m_Textures.push_back(textureImage);
        return TextureHandle{ static_cast<uint32_t>(m_Textures.size() - 1) };
    }

    ShaderHandle VulkanRHI::CreateShader(const ShaderDesc &) {
        return ShaderHandle();
    }

    void VulkanRHI::DestroyBuffer(BufferHandle handle) {
        if (!handle.IsValid()) return;
        VulkanBuffer& buffer = m_Buffers[handle.id];
        vmaDestroyBuffer(m_Allocator, buffer.buffer, buffer.allocation);
        buffer.buffer     = VK_NULL_HANDLE;
        buffer.allocation = VK_NULL_HANDLE;
        buffer.size       = 0;
    }

    void VulkanRHI::DestroyTexture(TextureHandle) {
    }

    void VulkanRHI::DestroyShader(ShaderHandle) {
    }

    void VulkanRHI::BindTexture(TextureHandle handle) {
        if (!handle.IsValid()) return;
        if (handle.id == m_BoundTexture.id) return; // already bound, skip
        m_BoundTexture = handle;
        vkQueueWaitIdle(m_Device.graphicsQueue); // wait for GPU to finish

        VulkanImage& texture = m_Textures[handle.id];

        VkDescriptorImageInfo imageInfo = {
            .sampler     = texture.sampler,
            .imageView   = texture.imageView,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };

        VkWriteDescriptorSet write = {
            .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet          = m_DescriptorSet,
            .dstBinding      = 1,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo      = &imageInfo,
        };

        vkUpdateDescriptorSets(m_Device.logicalDevice, 1, &write, 0, nullptr);
    }

    void VulkanRHI::BindPipeline(PipelineHandle pipeline) {
    }

    void VulkanRHI::Draw(uint32_t vertexCount) {
    }

    void VulkanRHI::DrawIndexed(uint32_t indexCount) {
        VkCommandBuffer cmd = m_Frames[m_CurrentFrame].commandBuffer;

        vkCmdPushConstants(cmd, m_PipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
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

    void VulkanRHI::Dispatch(uint32_t x, uint32_t y, uint32_t z) {
    }

    void VulkanRHI::UpdateCamera(const glm::mat4 &view, const glm::mat4 &projection) {
        struct CameraBuffer {
            glm::mat4 view;
            glm::mat4 projection;
        };
        const CameraBuffer camera_buffer = {.view = view, .projection = projection};
        UploadDynamicBuffer(m_CameraUniformBuffer, &camera_buffer, sizeof(CameraBuffer));
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

        VkDescriptorSetLayoutBinding descriptorSetLayoutBinding = {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        };
        VkDescriptorSetLayoutBinding samplerBinding = {
            .binding         = 1,
            .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT,
        };

        VkDescriptorSetLayoutBinding bindings[] = { descriptorSetLayoutBinding, samplerBinding };


        VkDescriptorSetLayoutCreateInfo descriptorSetLayoutInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = 2,
            .pBindings = bindings
        };
        VK_CHECK(vkCreateDescriptorSetLayout(m_Device.logicalDevice, &descriptorSetLayoutInfo, nullptr, &m_DescriptorLayout));

        VkPushConstantRange pushConstantRange = {
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .offset     = 0,
            .size       = sizeof(glm::mat4),
        };

        VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 1,
            .pSetLayouts = &m_DescriptorLayout,
            .pushConstantRangeCount = 1,
            .pPushConstantRanges    = &pushConstantRange,
        };

        VkResult result = vkCreatePipelineLayout(m_Device.logicalDevice, &pipelineLayoutInfo, nullptr, &m_PipelineLayout);
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
            .layout = m_PipelineLayout,
        };

        result = vkCreateGraphicsPipelines(m_Device.logicalDevice, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &m_GraphicsPipeline);
        VK_CHECK(result);

        return true;
    }

    bool VulkanRHI::CreateDescriptorPool() {
        VkDescriptorPoolSize poolSizes[] = {
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         1 },
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 },
        };
        const VkDescriptorPoolCreateInfo descriptorPoolInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = 1,
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
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = m_DescriptorPool,
            .descriptorSetCount = 1,
            .pSetLayouts = &m_DescriptorLayout,
        };

        VkResult result = vkAllocateDescriptorSets(m_Device.logicalDevice, &descriptorSetAllocateInfo, &m_DescriptorSet);

        if (result != VK_SUCCESS) {
            return false;
        }

        BufferDesc uniformBufferDesc = {
            .size = sizeof(glm::mat4) * 2,
            .usage = BufferUsage::Uniform,
            .cpuVisible= true,
        };

        m_CameraUniformBuffer = CreateBuffer(uniformBufferDesc);

        VkDescriptorBufferInfo bufferInfo = {
            .buffer = m_Buffers.at(m_CameraUniformBuffer.id).buffer,
            .offset = 0,
            .range = sizeof(glm::mat4) * 2,
        };
        const VkWriteDescriptorSet writeDescriptorSet = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = m_DescriptorSet,
            .dstBinding = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .pBufferInfo = &bufferInfo,
        };

        vkUpdateDescriptorSets(m_Device.logicalDevice, 1, &writeDescriptorSet, 0, nullptr);

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

    void VulkanRHI::RecordCommandBuffer(const VkCommandBuffer commandBuffer, const uint32_t imageIndex) const {
        constexpr VkCommandBufferBeginInfo beginInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        };
        vkBeginCommandBuffer(commandBuffer, &beginInfo);

        VkImageMemoryBarrier imageMemoryBarrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_NONE,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .image = m_SwapChain.swapChainImages[imageIndex],
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            },
        };

        VkImageMemoryBarrier depthBarrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_NONE,
            .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            .image = m_DepthImage.image,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        };

        VkRenderingAttachmentInfo depthAttachmentInfo = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = m_DepthImage.imageView,
            .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            .loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp     = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .clearValue  = {.depthStencil = {1.0f, 0}},
        };

        VkImageMemoryBarrier barriers[] = { imageMemoryBarrier, depthBarrier };

        vkCmdPipelineBarrier(commandBuffer,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
            0, 0, nullptr, 0, nullptr, 2, barriers);

        VkRenderingAttachmentInfo colorAttachment = {
            .sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView   = m_SwapChain.swapChainImageViews[imageIndex],
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp     = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue  = {.color = {0.0f, 0.0f, 0.0f, 1.0f}},
        };

        const VkRenderingInfo renderingInfo = {
            .sType                = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea           = {.offset = {0, 0}, .extent = m_SwapChain.swapChainExtent},
            .layerCount           = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments    = &colorAttachment,
            .pDepthAttachment     = &depthAttachmentInfo,
        };

        vkCmdBeginRendering(commandBuffer, &renderingInfo);

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_GraphicsPipeline);
        //vkCmdPushConstants(commandBuffer, m_PipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &m_ModelMatrix);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_PipelineLayout, 0, 1, &m_DescriptorSet, 0, nullptr);

        VkViewport viewport = {
            .x        = 0.0f,
            .y        = 0.0f,
            .width    = static_cast<float>(m_SwapChain.swapChainExtent.width),
            .height   = static_cast<float>(m_SwapChain.swapChainExtent.height),
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
        };
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

        VkRect2D scissor = {
            .offset = {0, 0},
            .extent = m_SwapChain.swapChainExtent,
        };
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

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
        vmaDestroyImage(m_Allocator, m_DepthImage.image, nullptr);
        m_DepthImage.image = VK_NULL_HANDLE;

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

    uint32_t VulkanRHI::AllocateBufferSlot(const VulkanBuffer &buffer) {
        for (uint32_t i = 0; i < m_Buffers.size(); i++) {
            if (m_Buffers.at(i).buffer == VK_NULL_HANDLE) {
                m_Buffers.at(i) = buffer;
                return  i;
            }
        }
        m_Buffers.push_back(buffer);
        return m_Buffers.size() - 1;
    }
} // Osiris