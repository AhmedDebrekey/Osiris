//
// Created by Debreky on 01/06/2026.
//
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
#include "VulkanRHI.h"

#include <algorithm>
#include <cfloat>
#include <fstream>
#include <SDL.h>
#include <SDL_vulkan.h>

#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_vulkan.h"

#include "VulkanUtils.h"
#include "core/AssetManager.h"
#include "core/Log.h"
#include "renderer/MeshType.h"
#include "assets/TextureLoader.h"

#include <glm/gtc/matrix_transform.hpp>


namespace Osiris {

    // Mirrors the CameraUBO layout in triangle.vert / triangle.frag (std140: mat4 and vec4
    // members are all naturally 16-byte aligned, so no explicit padding is required).
    struct CameraBufferFull {
        glm::mat4 view;
        glm::mat4 projection;
        glm::mat4 lightSpaceMatrices[3];
        glm::vec4 cascadeSplits;   // w = environment exposure (IBL), unused slot repurposed
        glm::vec4 lightDirection;
        glm::vec4 cameraPosition;
    };

    // Mirrors the SpotLightGPU struct in triangle.vert / triangle.frag.
    struct SpotLightGPU {
        glm::vec4 position;   // xyz = world position, w = range
        glm::vec4 direction;  // xyz = normalized direction, w = intensity
        glm::vec4 color;      // rgb = color
        glm::vec4 params;     // x = cos(inner), y = cos(outer), z = shadowIndex (-1 = none), w unused
    };

    // Mirrors the SpotLightUBO layout in triangle.vert / triangle.frag.
    struct SpotLightBufferFull {
        glm::mat4     shadowMatrices[MAX_SPOT_SHADOW_CASTERS];
        SpotLightGPU  lights[MAX_SPOT_LIGHTS];
        glm::ivec4    counts; // x = active light count
    };

    // Mirrors the push constant block in triangle.vert / triangle.frag.
    struct ForwardPushConstants {
        glm::mat4 model;
        glm::vec4 emissive; // rgb = color, w = intensity
    };

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

    VkFormat GetVulkanTextureFormat(TextureFormat format) {
        switch (format) {
        case TextureFormat::RGBA8_UNORM:
            return VK_FORMAT_R8G8B8A8_UNORM;
        case TextureFormat::RGBA8_SRGB:
            return VK_FORMAT_R8G8B8A8_SRGB;
        default:
            return VK_FORMAT_UNDEFINED;
        }
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

        VkPhysicalDeviceProperties physicalDeviceProperties;
        vkGetPhysicalDeviceProperties(m_Device.physicalDevice, &physicalDeviceProperties);
        m_TimestampPeriod = physicalDeviceProperties.limits.timestampPeriod;

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
            .vertexShader     = AssetManager::GetEnginePath("shaders/triangle.vert.spv"),
            .fragmentShader   = AssetManager::GetEnginePath("shaders/triangle.frag.spv"),
            .colorAttachment  = true,
            .colorFormat      = m_SwapChain.swapChainImageFormat,
            .depthAttachment  = true,
            .depthFormat      = VK_FORMAT_D32_SFLOAT,
            .depthTest        = true,
            .depthWrite       = true,
            .depthBias        = false,
            .cullMode         = VK_CULL_MODE_BACK_BIT,
            .frontFace        = VK_FRONT_FACE_COUNTER_CLOCKWISE,
            .setLayoutCount   = 2,
            .pSetLayouts      = forwardLayouts,
            .pushConstantSize = sizeof(ForwardPushConstants),
            .pushConstantStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        });

        m_ForwardPipelineLayout = m_PipelineManager->GetLayout(m_ForwardPipeline);

        VkDescriptorSetLayout shadowLayouts[] = { m_FrameDescriptorLayout };

        m_ShadowPipeline = m_PipelineManager->GetOrCreate({
            .vertexShader       = AssetManager::GetEnginePath("shaders/shadow.vert.spv"),
            .fragmentShader     = "",
            .colorAttachment    = false,
            .colorFormat        = VK_FORMAT_UNDEFINED,
            .depthAttachment    = true,
            .depthFormat        = VK_FORMAT_D32_SFLOAT,
            .depthTest          = true,
            .depthWrite         = true,
            .depthBias          = true,
            .cullMode           = VK_CULL_MODE_NONE,
            .frontFace          = VK_FRONT_FACE_CLOCKWISE,
            .setLayoutCount     = 1,
            .pSetLayouts        = shadowLayouts,
            .pushConstantSize   = sizeof(glm::mat4) * 2,
            .pushConstantStages = VK_SHADER_STAGE_VERTEX_BIT,
        });

        m_ShadowPipelineLayout = m_PipelineManager->GetLayout(m_ShadowPipeline);

        VkDescriptorSetLayout skyboxLayouts[] = { m_FrameDescriptorLayout };

        m_SkyboxPipeline = m_PipelineManager->GetOrCreate({
            .vertexShader     = AssetManager::GetEnginePath("shaders/skybox.vert.spv"),
            .fragmentShader   = AssetManager::GetEnginePath("shaders/skybox.frag.spv"),
            .colorAttachment  = true,
            .colorFormat      = m_SwapChain.swapChainImageFormat,
            .depthAttachment  = true,
            .depthFormat      = VK_FORMAT_D32_SFLOAT,
            .depthTest        = true,
            .depthWrite       = false,
            .depthCompareOp   = VK_COMPARE_OP_LESS_OR_EQUAL, // matches the clear value at the far plane
            .depthBias        = false,
            .cullMode         = VK_CULL_MODE_NONE,
            .vertexInput      = false, // hardcoded cube in skybox.vert, no vertex buffer
            .setLayoutCount   = 1,
            .pSetLayouts      = skyboxLayouts,
            .pushConstantSize = sizeof(float), // exposure
            .pushConstantStages = VK_SHADER_STAGE_FRAGMENT_BIT,
        });

        m_SkyboxPipelineLayout = m_PipelineManager->GetLayout(m_SkyboxPipeline);

        VkDescriptorSetLayout postProcessLayouts[] = { m_PostProcessDescriptorLayout };

        m_PostProcessPipeline = m_PipelineManager->GetOrCreate({
            .vertexShader     = AssetManager::GetEnginePath("shaders/postprocess.vert.spv"),
            .fragmentShader   = AssetManager::GetEnginePath("shaders/postprocess.frag.spv"),
            .colorAttachment  = true,
            .colorFormat      = m_SwapChain.swapChainImageFormat,
            .depthAttachment  = false,
            .depthFormat      = VK_FORMAT_UNDEFINED,
            .depthTest        = false,
            .depthWrite       = false,
            .depthBias        = false,
            .cullMode         = VK_CULL_MODE_NONE,
            .vertexInput      = false, // fullscreen triangle in postprocess.vert, no vertex buffer
            .setLayoutCount   = 1,
            .pSetLayouts      = postProcessLayouts,
            .pushConstantSize = sizeof(float), // wall-clock seconds, for animated film grain
            .pushConstantStages = VK_SHADER_STAGE_FRAGMENT_BIT,
        });

        m_PostProcessPipelineLayout = m_PipelineManager->GetLayout(m_PostProcessPipeline);

        if (!CreateDescriptorPool()) {
            OSIRIS_ERROR("Failed to create descriptor pool!");
            return false;
        }

        if (!CreateDescriptorSet()) {
            OSIRIS_ERROR("Failed to create descriptor set!");
            return false;
        }

        const VkSamplerCreateInfo postProcessSamplerInfo = {
            .sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter    = VK_FILTER_LINEAR,
            .minFilter    = VK_FILTER_LINEAR,
            .mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR,
            .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .anisotropyEnable = VK_FALSE,
            .maxAnisotropy    = 1.0f,
            .compareEnable    = VK_FALSE,
            .borderColor      = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
            .unnormalizedCoordinates = VK_FALSE,
        };
        VK_CHECK(vkCreateSampler(m_Device.logicalDevice, &postProcessSamplerInfo, nullptr, &m_PostProcessSampler));

        std::array<VkDescriptorSetLayout, MAX_FRAMES_IN_FLIGHT> postProcessSetLayouts;
        postProcessSetLayouts.fill(m_PostProcessDescriptorLayout);
        const VkDescriptorSetAllocateInfo postProcessSetAllocInfo = {
            .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool     = m_DescriptorPool,
            .descriptorSetCount = MAX_FRAMES_IN_FLIGHT,
            .pSetLayouts        = postProcessSetLayouts.data(),
        };
        VK_CHECK(vkAllocateDescriptorSets(m_Device.logicalDevice, &postProcessSetAllocInfo, m_PostProcessDescriptorSets.data()));

        const BufferDesc postProcessSettingsBufferDesc = {
            .size       = sizeof(PostProcessSettings),
            .usage      = BufferUsage::Uniform,
            .cpuVisible = true,
        };
        m_PostProcessSettingsBuffer = CreateBuffer(postProcessSettingsBufferDesc);

        // Binding 1 points at the same shared settings buffer every frame, its contents change
        // via UploadDynamicBuffer (a host-visible memory write, not a validated "in use" Vulkan
        // call) rather than the buffer identity ever changing, so unlike binding 0's sampler
        // (rewritten every DrawPostProcessFullscreen call) this only needs writing once per slot.
        const VkDescriptorBufferInfo postProcessSettingsBufferInfo = {
            .buffer = m_Buffers.at(m_PostProcessSettingsBuffer.id).buffer,
            .offset = 0,
            .range  = sizeof(PostProcessSettings),
        };
        for (uint32_t frame = 0; frame < MAX_FRAMES_IN_FLIGHT; frame++) {
            const VkWriteDescriptorSet settingsWrite = {
                .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet          = m_PostProcessDescriptorSets[frame],
                .dstBinding      = 1,
                .descriptorCount = 1,
                .descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .pBufferInfo     = &postProcessSettingsBufferInfo,
            };
            vkUpdateDescriptorSets(m_Device.logicalDevice, 1, &settingsWrite, 0, nullptr);
        }

        if (!CreateSceneColorImage()) {
            OSIRIS_ERROR("Failed to create scene color image!");
            return false;
        }

        if (!CreateCommandBuffers()) {
            OSIRIS_ERROR("Failed to create command buffers!");
            return false;
        }

        constexpr VkQueryPoolCreateInfo queryPoolInfo = {
            .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
            .queryType = VK_QUERY_TYPE_TIMESTAMP,
            .queryCount = GPU_TIMESTAMP_QUERY_COUNT,
        };
        for (auto& timestampFrame : m_GPUTimestampFrames) {
            if (vkCreateQueryPool(m_Device.logicalDevice, &queryPoolInfo, nullptr,
                &timestampFrame.queryPool) != VK_SUCCESS) {
                OSIRIS_ERROR("Failed to create GPU timestamp query pool");
                return false;
            }
        }

        if (!CreateShadowMap()) {
            OSIRIS_ERROR("Failed to create Shadow Map");
            return false;
        }

        UpdateShadowDescriptors();

        if (!GenerateBRDFLUT()) {
            OSIRIS_ERROR("Failed to generate BRDF LUT");
            return false;
        }

        if (!CreateDefaultEnvironmentCubemap()) {
            OSIRIS_ERROR("Failed to create default environment cubemap");
            return false;
        }

        m_ColorBufferRG = RGTexture{0};
        m_DepthBufferRG = RGTexture{1};
        m_DefaultAlbedo     = CreateSolidColorTexture(255, 255, 255, 255, TextureFormat::RGBA8_SRGB);
        m_DefaultNormal     = CreateSolidColorTexture(128, 128, 255, 255, TextureFormat::RGBA8_UNORM);
        m_DefaultMetallic   = CreateSolidColorTexture(000, 000, 000, 255, TextureFormat::RGBA8_UNORM);
        m_DefaultRoughness  = CreateSolidColorTexture(128, 128, 128, 255, TextureFormat::RGBA8_UNORM);
        m_DefaultAO         = CreateSolidColorTexture(255, 255, 255, 255, TextureFormat::RGBA8_UNORM);

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
        for (const auto& timestampFrame : m_GPUTimestampFrames) {
            vkDestroyQueryPool(m_Device.logicalDevice, timestampFrame.queryPool, nullptr);
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

        for (auto& shadowMap : m_SpotShadowMaps) {
            if (shadowMap.image != VK_NULL_HANDLE) {
                vkDestroySampler(m_Device.logicalDevice, shadowMap.sampler, nullptr);
                vkDestroyImageView(m_Device.logicalDevice, shadowMap.imageView, nullptr);
                vmaDestroyImage(m_Allocator, shadowMap.image, shadowMap.allocation);
            }
        }

        if (m_BRDFLut.image != VK_NULL_HANDLE) {
            vkDestroySampler(m_Device.logicalDevice, m_BRDFLut.sampler, nullptr);
            vkDestroyImageView(m_Device.logicalDevice, m_BRDFLut.imageView, nullptr);
            vmaDestroyImage(m_Allocator, m_BRDFLut.image, m_BRDFLut.allocation);
        }

        if (m_EnvironmentCubemap.image != VK_NULL_HANDLE) {
            vkDestroySampler(m_Device.logicalDevice, m_EnvironmentCubemap.sampler, nullptr);
            vkDestroyImageView(m_Device.logicalDevice, m_EnvironmentCubemap.sampledView, nullptr);
            vkDestroyImageView(m_Device.logicalDevice, m_EnvironmentCubemap.storageView, nullptr);
            vmaDestroyImage(m_Allocator, m_EnvironmentCubemap.image, m_EnvironmentCubemap.allocation);
        }

        if (m_DefaultEnvironmentCubemap.image != VK_NULL_HANDLE) {
            vkDestroySampler(m_Device.logicalDevice, m_DefaultEnvironmentCubemap.sampler, nullptr);
            vkDestroyImageView(m_Device.logicalDevice, m_DefaultEnvironmentCubemap.sampledView, nullptr);
            vmaDestroyImage(m_Allocator, m_DefaultEnvironmentCubemap.image, m_DefaultEnvironmentCubemap.allocation);
        }

        if (m_IrradianceCubemap.image != VK_NULL_HANDLE) {
            vkDestroySampler(m_Device.logicalDevice, m_IrradianceCubemap.sampler, nullptr);
            vkDestroyImageView(m_Device.logicalDevice, m_IrradianceCubemap.sampledView, nullptr);
            vkDestroyImageView(m_Device.logicalDevice, m_IrradianceCubemap.storageView, nullptr);
            vmaDestroyImage(m_Allocator, m_IrradianceCubemap.image, m_IrradianceCubemap.allocation);
        }

        if (m_PrefilteredCubemap.image != VK_NULL_HANDLE) {
            vkDestroySampler(m_Device.logicalDevice, m_PrefilteredCubemap.sampler, nullptr);
            vkDestroyImageView(m_Device.logicalDevice, m_PrefilteredCubemap.sampledView, nullptr);
            vmaDestroyImage(m_Allocator, m_PrefilteredCubemap.image, m_PrefilteredCubemap.allocation);
        }
        for (auto& mipView : m_PrefilterMipViews) {
            vkDestroyImageView(m_Device.logicalDevice, mipView, nullptr); // already null in the normal path
        }

        DestroyViewportResources();
        DestroySceneColorImage();
        vkDestroySampler(m_Device.logicalDevice, m_PostProcessSampler, nullptr);

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
        vkDestroyDescriptorSetLayout(m_Device.logicalDevice, m_PostProcessDescriptorLayout, nullptr);
        vkDestroyDescriptorSetLayout(m_Device.logicalDevice, m_BRDFLutComputeLayout, nullptr);
        vkDestroyDescriptorSetLayout(m_Device.logicalDevice, m_EquirectToCubemapLayout, nullptr);
        vkDestroyDescriptorSetLayout(m_Device.logicalDevice, m_IrradianceConvolveLayout, nullptr);
        vkDestroyDescriptorSetLayout(m_Device.logicalDevice, m_PrefilterConvolveLayout, nullptr);

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

        auto& timestampFrame = m_GPUTimestampFrames[m_CurrentFrame];
        if (timestampFrame.queryCount > 0) {
            std::vector<uint64_t> queryResults(timestampFrame.queryCount);
            const VkResult result = vkGetQueryPoolResults(
                m_Device.logicalDevice,
                timestampFrame.queryPool,
                0,
                timestampFrame.queryCount,
                queryResults.size() * sizeof(uint64_t),
                queryResults.data(),
                sizeof(uint64_t),
                VK_QUERY_RESULT_64_BIT);
            if (result == VK_SUCCESS) {
                m_GPUTimings.clear();
                for (const auto& scope : timestampFrame.scopes) {
                    if (scope.endQuery == UINT32_MAX) continue;
                    const uint64_t elapsedTicks = queryResults[scope.endQuery] - queryResults[scope.startQuery];
                    const float elapsedMilliseconds = static_cast<float>(elapsedTicks) * m_TimestampPeriod / 1'000'000.0f;
                    m_GPUTimings.emplace_back(scope.name, elapsedMilliseconds);
                }
            } else {
                OSIRIS_WARN("Failed to resolve GPU timestamp queries: {}", static_cast<int>(result));
            }
        }

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
        vkCmdResetQueryPool(m_Frames[m_CurrentFrame].commandBuffer, timestampFrame.queryPool,
            0, GPU_TIMESTAMP_QUERY_COUNT);
        timestampFrame.queryCount = 0;
        timestampFrame.scopes.clear();

        m_FrameStarted = true;
    }

    void VulkanRHI::BeginGPUTimestamp(const std::string& name) {
        if (!m_FrameStarted) return;

        auto& timestampFrame = m_GPUTimestampFrames[m_CurrentFrame];
        if (timestampFrame.queryCount >= GPU_TIMESTAMP_QUERY_COUNT) {
            OSIRIS_WARN("GPU timestamp query pool is full");
            return;
        }

        const uint32_t query = timestampFrame.queryCount++;
        vkCmdWriteTimestamp(m_Frames[m_CurrentFrame].commandBuffer,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, timestampFrame.queryPool, query);
        timestampFrame.scopes.push_back({
            .name = name,
            .startQuery = query,
        });
    }

    void VulkanRHI::EndGPUTimestamp(const std::string& name) {
        if (!m_FrameStarted) return;

        auto& timestampFrame = m_GPUTimestampFrames[m_CurrentFrame];
        const auto scope = std::ranges::find_if(timestampFrame.scopes.rbegin(), timestampFrame.scopes.rend(),
            [&name](const GPUTimestampScope& candidate) {
                return candidate.name == name && candidate.endQuery == UINT32_MAX;
            });
        if (scope == timestampFrame.scopes.rend()) {
            OSIRIS_WARN("No open GPU timestamp scope named '{}'", name);
            return;
        }
        if (timestampFrame.queryCount >= GPU_TIMESTAMP_QUERY_COUNT) {
            OSIRIS_WARN("GPU timestamp query pool is full");
            return;
        }

        scope->endQuery = timestampFrame.queryCount++;
        vkCmdWriteTimestamp(m_Frames[m_CurrentFrame].commandBuffer,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestampFrame.queryPool, scope->endQuery);
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

    void VulkanRHI::SetEmissive(const glm::vec3& color, float intensity) {
        m_EmissiveColor = color;
        m_EmissiveIntensity = intensity;
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

    void VulkanRHI::GenerateMipmaps(VkCommandBuffer cmd, VkImage image,
                                    uint32_t width, uint32_t height, uint32_t mipLevels) {
        VkImageMemoryBarrier barrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        };

        int32_t mipWidth = static_cast<int32_t>(width);
        int32_t mipHeight = static_cast<int32_t>(height);
        for (uint32_t mip = 1; mip < mipLevels; ++mip) {
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.subresourceRange.baseMipLevel = mip - 1;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &barrier);

            const int32_t nextWidth = std::max(mipWidth / 2, 1);
            const int32_t nextHeight = std::max(mipHeight / 2, 1);
            const VkImageBlit blit = {
                .srcSubresource = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .mipLevel = mip - 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
                .srcOffsets = {{0, 0, 0}, {mipWidth, mipHeight, 1}},
                .dstSubresource = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .mipLevel = mip,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
                .dstOffsets = {{0, 0, 0}, {nextWidth, nextHeight, 1}},
            };
            vkCmdBlitImage(cmd,
                image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1, &blit, VK_FILTER_LINEAR);

            barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &barrier);

            mipWidth = nextWidth;
            mipHeight = nextHeight;
        }

        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.subresourceRange.baseMipLevel = mipLevels - 1;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    TextureHandle VulkanRHI::CreateTexture(const TextureDesc& desc) {
        if (!desc.pixels || desc.width == 0 || desc.height == 0 || desc.dataSize == 0) {
            OSIRIS_ERROR("CreateTexture received invalid pixel data or dimensions");
            return {};
        }

        const VkFormat format = GetVulkanTextureFormat(desc.format);
        if (format == VK_FORMAT_UNDEFINED) {
            OSIRIS_ERROR("CreateTexture does not support texture format {}",
                static_cast<uint32_t>(desc.format));
            return {};
        }

        uint32_t maxMipLevels = 1;
        for (uint32_t dimension = std::max(desc.width, desc.height); dimension > 1; dimension /= 2) {
            ++maxMipLevels;
        }
        uint32_t mipLevels = std::clamp(desc.mipLevels, 1u, maxMipLevels);

        if (mipLevels > 1) {
            VkFormatProperties formatProperties;
            vkGetPhysicalDeviceFormatProperties(m_Device.physicalDevice, format, &formatProperties);
            constexpr VkFormatFeatureFlags requiredFeatures =
                VK_FORMAT_FEATURE_BLIT_SRC_BIT |
                VK_FORMAT_FEATURE_BLIT_DST_BIT |
                VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
            if ((formatProperties.optimalTilingFeatures & requiredFeatures) != requiredFeatures) {
                OSIRIS_WARN("Texture format {} cannot generate filtered mipmaps on this GPU",
                    static_cast<uint32_t>(desc.format));
                mipLevels = 1;
            }
        }

        const VkImageCreateInfo imageCreateInfo = {
            .sType       = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType   = VK_IMAGE_TYPE_2D,
            .format      = format,
            .extent      = {desc.width, desc.height, 1},
            .mipLevels   = mipLevels,
            .arrayLayers = 1,
            .samples     = VK_SAMPLE_COUNT_1_BIT,
            .tiling      = VK_IMAGE_TILING_OPTIMAL,
            .usage       = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                           VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                           VK_IMAGE_USAGE_SAMPLED_BIT,
        };

        const VmaAllocationCreateInfo allocationCreateInfo = {
            .usage = VMA_MEMORY_USAGE_GPU_ONLY,
        };

        VulkanImage textureImage;
        textureImage.format = format;
        VK_CHECK(vmaCreateImage(m_Allocator, &imageCreateInfo, &allocationCreateInfo,
            &textureImage.image, &textureImage.allocation, nullptr));

        const BufferDesc stagingDesc = {
            .size       = desc.dataSize,
            .usage      = BufferUsage::Transfer,
            .cpuVisible = true,
        };
        const BufferHandle stagingHandle = CreateBuffer(stagingDesc);
        memcpy(m_Buffers[stagingHandle.id].allocationInfo.pMappedData, desc.pixels, desc.dataSize);

        VkCommandBuffer cmd = BeginOneTimeCommands();
        const VkImageMemoryBarrier toTransferDestination = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_NONE,
            .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = textureImage.image,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = mipLevels,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        };
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &toTransferDestination);

        const VkBufferImageCopy copyRegion = {
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

        GenerateMipmaps(cmd, textureImage.image, desc.width, desc.height, mipLevels);
        EndOneTimeCommands(cmd);
        DestroyBuffer(stagingHandle);

        const VkImageViewCreateInfo viewCreateInfo = {
            .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image    = textureImage.image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format   = format,
            .subresourceRange = {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel   = 0,
                .levelCount     = mipLevels,
                .baseArrayLayer = 0,
                .layerCount     = 1,
            },
        };
        VK_CHECK(vkCreateImageView(m_Device.logicalDevice, &viewCreateInfo, nullptr,
            &textureImage.imageView));

        const VkSamplerCreateInfo samplerInfo = {
            .sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter    = VK_FILTER_LINEAR,
            .minFilter    = VK_FILTER_LINEAR,
            .mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR,
            .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
            .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
            .anisotropyEnable = m_SamplerAnisotropyEnabled ? VK_TRUE : VK_FALSE,
            .maxAnisotropy    = m_MaxSamplerAnisotropy,
            .compareEnable    = VK_FALSE,
            .minLod           = 0.0f,
            .maxLod           = static_cast<float>(mipLevels - 1),
            .borderColor      = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
            .unnormalizedCoordinates = VK_FALSE,
        };
        VK_CHECK(vkCreateSampler(m_Device.logicalDevice, &samplerInfo, nullptr,
            &textureImage.sampler));

        const uint32_t index = AllocateSlot(m_Textures, textureImage,
            [](const VulkanImage& texture) { return texture.image == VK_NULL_HANDLE; });
        return TextureHandle{index};
    }

    ShaderHandle VulkanRHI::CreateShader(const ShaderDesc& desc) {
        return ShaderHandle();
    }

    void VulkanRHI::WriteToDescriptorSet(TextureHandle textureHandle, uint32_t dstBinding, VkDescriptorSet& descriptorSet) {
        VulkanImage& texture = m_Textures[textureHandle.id];
        VkDescriptorImageInfo imageInfo = {
            .sampler     = texture.sampler,
            .imageView   = texture.imageView,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };
        VkWriteDescriptorSet write = {
            .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet          = descriptorSet,
            .dstBinding      = dstBinding,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo      = &imageInfo,
        };
        vkUpdateDescriptorSets(m_Device.logicalDevice, 1, &write, 0, nullptr);
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
        TextureHandle albedo;
        if (!desc.albedo.IsValid()) {
            albedo = m_DefaultAlbedo;
        }else {
            albedo = desc.albedo;
        }
        WriteToDescriptorSet(albedo, 0, materialSet);

        TextureHandle normal;
        if (!desc.normal.IsValid()) {
            normal = m_DefaultNormal;
        } else {
            normal = desc.normal;
        }
        WriteToDescriptorSet(normal, 1, materialSet);

        TextureHandle metallic;
        if (!desc.metallic.IsValid()) {
            metallic = m_DefaultMetallic;
        } else {
            metallic = desc.metallic;
        }
        WriteToDescriptorSet(metallic, 2, materialSet);

        TextureHandle roughness;
        if (!desc.roughness.IsValid()) {
            roughness = m_DefaultRoughness;
        } else {
            roughness = desc.roughness;
        }
        WriteToDescriptorSet(roughness, 3, materialSet);

        TextureHandle ao;
        if (!desc.ao.IsValid()) {
            ao = m_DefaultAO;
        } else {
            ao = desc.ao;
        }
        WriteToDescriptorSet(ao, 4, materialSet);

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

        const ForwardPushConstants push = {
            .model = m_ModelMatrix,
            .emissive = glm::vec4(m_EmissiveColor, m_EmissiveIntensity),
        };
        vkCmdPushConstants(cmd, m_ForwardPipelineLayout,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0, sizeof(ForwardPushConstants), &push);

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

    // Dark charcoal + warm amber accent, sharp-ish corners — reserves the accent color for actual
    // selection/active-state feedback (checkmarks, sliders, selected rows, the focused dock tab's
    // overline) rather than tinting every hover, so it doesn't wash the whole editor orange.
    void VulkanRHI::ApplyEditorTheme() {
        ImGuiStyle& style = ImGui::GetStyle();

        style.WindowRounding    = 3.0f;
        style.ChildRounding     = 3.0f;
        style.FrameRounding     = 2.0f;
        style.PopupRounding     = 3.0f;
        style.ScrollbarRounding = 3.0f;
        style.GrabRounding      = 2.0f;
        style.TabRounding       = 2.0f;

        style.WindowBorderSize = 1.0f;
        style.FrameBorderSize  = 1.0f;
        style.PopupBorderSize  = 1.0f;

        style.WindowPadding    = ImVec2(8.0f, 8.0f);
        style.FramePadding     = ImVec2(6.0f, 4.0f);
        style.ItemSpacing      = ImVec2(6.0f, 6.0f);
        style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
        style.IndentSpacing    = 16.0f;
        style.ScrollbarSize    = 14.0f;
        style.GrabMinSize      = 10.0f;
        style.WindowTitleAlign = ImVec2(0.0f, 0.5f);

        const ImVec4 bgDarkest    = ImVec4(0.07f, 0.07f, 0.075f, 1.00f);
        const ImVec4 bg           = ImVec4(0.122f, 0.122f, 0.130f, 1.00f);
        const ImVec4 bgLight      = ImVec4(0.16f, 0.16f, 0.17f, 1.00f);
        const ImVec4 frameBg      = ImVec4(0.16f, 0.16f, 0.17f, 1.00f);
        const ImVec4 frameHovered = ImVec4(0.23f, 0.23f, 0.24f, 1.00f);
        const ImVec4 border       = ImVec4(0.05f, 0.05f, 0.05f, 0.50f);
        const ImVec4 text         = ImVec4(0.88f, 0.88f, 0.88f, 1.00f);
        const ImVec4 textDisabled = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
        const ImVec4 accent       = ImVec4(0.92f, 0.55f, 0.10f, 1.00f);
        const ImVec4 accentHovered= ImVec4(1.00f, 0.65f, 0.20f, 1.00f);

        ImVec4* colors = style.Colors;
        colors[ImGuiCol_Text]                  = text;
        colors[ImGuiCol_TextDisabled]          = textDisabled;
        colors[ImGuiCol_WindowBg]              = bg;
        colors[ImGuiCol_ChildBg]               = ImVec4(0, 0, 0, 0);
        colors[ImGuiCol_PopupBg]               = ImVec4(bgLight.x, bgLight.y, bgLight.z, 0.98f);
        colors[ImGuiCol_Border]                = border;
        colors[ImGuiCol_BorderShadow]          = ImVec4(0, 0, 0, 0);
        colors[ImGuiCol_FrameBg]               = frameBg;
        colors[ImGuiCol_FrameBgHovered]        = frameHovered;
        colors[ImGuiCol_FrameBgActive]         = ImVec4(0.30f, 0.24f, 0.16f, 1.00f);
        colors[ImGuiCol_TitleBg]               = bgDarkest;
        colors[ImGuiCol_TitleBgActive]         = bgDarkest;
        colors[ImGuiCol_TitleBgCollapsed]      = ImVec4(bgDarkest.x, bgDarkest.y, bgDarkest.z, 0.75f);
        colors[ImGuiCol_MenuBarBg]             = ImVec4(0.09f, 0.09f, 0.095f, 1.00f);
        colors[ImGuiCol_ScrollbarBg]           = ImVec4(0.09f, 0.09f, 0.095f, 1.00f);
        colors[ImGuiCol_ScrollbarGrab]         = ImVec4(0.25f, 0.25f, 0.26f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.32f, 0.32f, 0.33f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabActive]   = accent;
        colors[ImGuiCol_CheckMark]             = accent;
        colors[ImGuiCol_SliderGrab]            = ImVec4(0.85f, 0.50f, 0.10f, 1.00f);
        colors[ImGuiCol_SliderGrabActive]      = accentHovered;
        colors[ImGuiCol_Button]                = ImVec4(0.18f, 0.18f, 0.19f, 1.00f);
        colors[ImGuiCol_ButtonHovered]         = frameHovered;
        colors[ImGuiCol_ButtonActive]          = accent;
        colors[ImGuiCol_Header]                = ImVec4(accent.x, accent.y, accent.z, 0.35f);
        colors[ImGuiCol_HeaderHovered]         = ImVec4(accent.x, accent.y, accent.z, 0.20f);
        colors[ImGuiCol_HeaderActive]          = ImVec4(accent.x, accent.y, accent.z, 0.55f);
        colors[ImGuiCol_Separator]             = border;
        colors[ImGuiCol_SeparatorHovered]      = ImVec4(accent.x, accent.y, accent.z, 0.50f);
        colors[ImGuiCol_SeparatorActive]       = accent;
        colors[ImGuiCol_ResizeGrip]            = ImVec4(0.30f, 0.30f, 0.31f, 0.30f);
        colors[ImGuiCol_ResizeGripHovered]     = ImVec4(accent.x, accent.y, accent.z, 0.50f);
        colors[ImGuiCol_ResizeGripActive]      = ImVec4(accent.x, accent.y, accent.z, 0.80f);
        colors[ImGuiCol_Tab]                   = ImVec4(0.10f, 0.10f, 0.105f, 1.00f);
        colors[ImGuiCol_TabHovered]            = ImVec4(accent.x, accent.y, accent.z, 0.40f);
        colors[ImGuiCol_TabSelected]           = bgLight;
        colors[ImGuiCol_TabSelectedOverline]   = accent;
        colors[ImGuiCol_TabDimmed]             = ImVec4(0.08f, 0.08f, 0.085f, 1.00f);
        colors[ImGuiCol_TabDimmedSelected]     = ImVec4(0.12f, 0.12f, 0.13f, 1.00f);
        colors[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(0.50f, 0.30f, 0.08f, 1.00f);
        colors[ImGuiCol_DockingPreview]        = ImVec4(accent.x, accent.y, accent.z, 0.40f);
        colors[ImGuiCol_DockingEmptyBg]        = bgDarkest;
        colors[ImGuiCol_PlotLines]             = ImVec4(0.60f, 0.60f, 0.60f, 1.00f);
        colors[ImGuiCol_PlotLinesHovered]      = accent;
        colors[ImGuiCol_PlotHistogram]         = ImVec4(0.80f, 0.50f, 0.15f, 1.00f);
        colors[ImGuiCol_PlotHistogramHovered]  = accentHovered;
        colors[ImGuiCol_TableHeaderBg]         = ImVec4(0.14f, 0.14f, 0.15f, 1.00f);
        colors[ImGuiCol_TableBorderStrong]     = ImVec4(0.05f, 0.05f, 0.05f, 1.00f);
        colors[ImGuiCol_TableBorderLight]      = ImVec4(0.09f, 0.09f, 0.09f, 1.00f);
        colors[ImGuiCol_TableRowBg]            = ImVec4(0, 0, 0, 0);
        colors[ImGuiCol_TableRowBgAlt]         = ImVec4(1.00f, 1.00f, 1.00f, 0.02f);
        colors[ImGuiCol_TextSelectedBg]        = ImVec4(accent.x, accent.y, accent.z, 0.35f);
        colors[ImGuiCol_DragDropTarget]        = ImVec4(accent.x, accent.y, accent.z, 0.90f);
        colors[ImGuiCol_NavCursor]             = accent;
        colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
        colors[ImGuiCol_NavWindowingDimBg]     = ImVec4(0.20f, 0.20f, 0.20f, 0.50f);
        colors[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.05f, 0.05f, 0.05f, 0.60f);
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
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        ApplyEditorTheme();

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

        for (uint32_t i = 0; i < SHADOW_CASCADE_COUNT; i++) {
            const VkDescriptorSet descriptorSet = ImGui_ImplVulkan_AddTexture(
                m_ShadowMaps[i].imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            m_ShadowCascadeTextureIDs[i] = reinterpret_cast<uint64_t>(descriptorSet);
        }
        for (uint32_t i = 0; i < MAX_SPOT_SHADOW_CASTERS; i++) {
            const VkDescriptorSet descriptorSet = ImGui_ImplVulkan_AddTexture(
                m_SpotShadowMaps[i].imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            m_SpotShadowTextureIDs[i] = reinterpret_cast<uint64_t>(descriptorSet);
        }

        const std::pair<EditorIcon, const char*> editorIcons[] = {
            {EditorIcon::Folder,    "icons/folder.png"},
            {EditorIcon::GltfModel, "icons/gltfIcon.png"},
            {EditorIcon::JsonScene, "icons/jsonIcon.png"},
            {EditorIcon::LuaScript, "icons/luaIcon.png"},
            {EditorIcon::WavAudio,  "icons/wav-icon.png"},
        };
        for (const auto& [icon, relativePath] : editorIcons) {
            const TextureHandle iconTexture = TextureLoader::LoadFromFile(AssetManager::GetEnginePath(relativePath), this);
            if (!iconTexture.IsValid()) continue;
            const VkDescriptorSet descriptorSet = ImGui_ImplVulkan_AddTexture(
                m_Textures[iconTexture.id].imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            m_EditorIconTextureIDs[static_cast<size_t>(icon)] = reinterpret_cast<uint64_t>(descriptorSet);
        }
    }

    void VulkanRHI::ShutdownImGui() {
        vkDeviceWaitIdle(m_Device.logicalDevice);
        for (auto& textureID : m_ShadowCascadeTextureIDs) {
            if (textureID == 0) continue;
            ImGui_ImplVulkan_RemoveTexture(reinterpret_cast<VkDescriptorSet>(textureID));
            textureID = 0;
        }
        for (auto& textureID : m_SpotShadowTextureIDs) {
            if (textureID == 0) continue;
            ImGui_ImplVulkan_RemoveTexture(reinterpret_cast<VkDescriptorSet>(textureID));
            textureID = 0;
        }
        for (auto& textureID : m_EditorIconTextureIDs) {
            if (textureID == 0) continue;
            ImGui_ImplVulkan_RemoveTexture(reinterpret_cast<VkDescriptorSet>(textureID));
            textureID = 0;
        }
        if (m_ViewportTextureID != 0) {
            ImGui_ImplVulkan_RemoveTexture(reinterpret_cast<VkDescriptorSet>(m_ViewportTextureID));
            m_ViewportTextureID = 0;
        }
        if (m_PostProcessPreviewTextureID != 0) {
            ImGui_ImplVulkan_RemoveTexture(reinterpret_cast<VkDescriptorSet>(m_PostProcessPreviewTextureID));
            m_PostProcessPreviewTextureID = 0;
        }
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

    void VulkanRHI::RenderImGui(bool separatePass) {
        ImGui::Render();
        // A swapchain-recreate bailout in BeginFrame() leaves this frame's command buffer never
        // put into the recording state; skip the rest the same way EndFrame() already does rather
        // than issue vkCmd* calls against a buffer nothing began recording this frame.
        if (!m_FrameStarted) return;
        VkCommandBuffer cmd = m_Frames[m_CurrentFrame].commandBuffer;

        if (separatePass && m_RenderingViewport) {
            vkCmdEndRendering(cmd);

            m_RenderGraph.Reset();
            m_RenderGraph.ImportTexture(m_ColorBufferRG, m_ViewportColorImage.image, ResourceState::ColorWrite);
            m_RenderGraph.AddPass("ViewportSamplePass", PassType::Graphics)
                .Read({m_ColorBufferRG, ResourceState::ShaderRead})
                .SetExecute(nullptr);
            m_RenderGraph.Compile();
            m_RenderGraph.Execute(cmd);
            m_ViewportImageInitialized = true;

            // Edit-mode debug preview: never touches m_ViewportColorImage or what the normal
            // viewport shows, this renders the post-processed result into a separate image that
            // Editor.cpp only displays when GetPostProcessPreviewEnabled() is on. m_ViewportColorImage
            // is already ShaderRead from the pass above, exactly what this needs to sample it.
            if (m_PostProcessPreviewEnabled) {
                m_RenderGraph.Reset();
                m_RenderGraph.ImportTexture(m_ColorBufferRG, m_PostProcessPreviewImage.image, ResourceState::Undefined);
                m_RenderGraph.AddPass("PostProcessPreviewPass", PassType::Graphics)
                    .Write({m_ColorBufferRG, ResourceState::ColorWrite})
                    .SetExecute(nullptr);
                m_RenderGraph.Compile();
                m_RenderGraph.Execute(cmd);

                const VkRenderingAttachmentInfo previewAttachment = {
                    .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                    .imageView = m_PostProcessPreviewImage.imageView,
                    .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                    .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                };
                const VkRenderingInfo previewRenderingInfo = {
                    .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                    .renderArea = {.offset = {0, 0}, .extent = m_ViewportExtent},
                    .layerCount = 1,
                    .colorAttachmentCount = 1,
                    .pColorAttachments = &previewAttachment,
                };
                vkCmdBeginRendering(cmd, &previewRenderingInfo);
                DrawPostProcessFullscreen(cmd, m_ViewportColorImage.imageView, m_ViewportExtent);
                vkCmdEndRendering(cmd);

                m_RenderGraph.Reset();
                m_RenderGraph.ImportTexture(m_ColorBufferRG, m_PostProcessPreviewImage.image, ResourceState::ColorWrite);
                m_RenderGraph.AddPass("PostProcessPreviewSamplePass", PassType::Graphics)
                    .Read({m_ColorBufferRG, ResourceState::ShaderRead})
                    .SetExecute(nullptr);
                m_RenderGraph.Compile();
                m_RenderGraph.Execute(cmd);
            }

            m_RenderGraph.Reset();
            m_RenderGraph.ImportTexture(m_ColorBufferRG,
                m_SwapChain.swapChainImages[m_ImageIndex], ResourceState::Undefined);
            m_RenderGraph.ImportTexture(m_DepthBufferRG, m_DepthImage.image, ResourceState::Undefined);
            m_RenderGraph.AddPass("EditorUIPass", PassType::Graphics)
                .Write({m_ColorBufferRG, ResourceState::ColorWrite})
                .Write({m_DepthBufferRG, ResourceState::DepthWrite})
                .SetExecute(nullptr);
            m_RenderGraph.Compile();
            m_RenderGraph.Execute(cmd);

            VkRenderingAttachmentInfo colorAttachment = {
                .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                .imageView = m_SwapChain.swapChainImageViews[m_ImageIndex],
                .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .clearValue = {.color = {0.05f, 0.05f, 0.05f, 1.0f}},
            };
            VkRenderingAttachmentInfo depthAttachment = {
                .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                .imageView = m_DepthImage.imageView,
                .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            };
            const VkRenderingInfo renderingInfo = {
                .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                .renderArea = {.offset = {0, 0}, .extent = m_SwapChain.swapChainExtent},
                .layerCount = 1,
                .colorAttachmentCount = 1,
                .pColorAttachments = &colorAttachment,
                .pDepthAttachment = &depthAttachment,
            };
            vkCmdBeginRendering(cmd, &renderingInfo);
            m_RenderingViewport = false;
        } else if (!separatePass) {
            // Play mode: post-processing always applies, no toggle. The forward pass rendered
            // into m_SceneColorImage instead of the swapchain (see BeginForwardPass) specifically
            // so there's something to sample here before the swapchain becomes the final image.
            vkCmdEndRendering(cmd);

            m_RenderGraph.Reset();
            m_RenderGraph.ImportTexture(m_ColorBufferRG, m_SceneColorImage.image, ResourceState::ColorWrite);
            m_RenderGraph.AddPass("SceneColorSamplePass", PassType::Graphics)
                .Read({m_ColorBufferRG, ResourceState::ShaderRead})
                .SetExecute(nullptr);
            m_RenderGraph.Compile();
            m_RenderGraph.Execute(cmd);

            m_RenderGraph.Reset();
            m_RenderGraph.ImportTexture(m_ColorBufferRG,
                m_SwapChain.swapChainImages[m_ImageIndex], ResourceState::Undefined);
            m_RenderGraph.AddPass("PostProcessPass", PassType::Graphics)
                .Write({m_ColorBufferRG, ResourceState::ColorWrite})
                .SetExecute(nullptr);
            m_RenderGraph.Compile();
            m_RenderGraph.Execute(cmd);

            const VkRenderingAttachmentInfo postProcessAttachment = {
                .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                .imageView = m_SwapChain.swapChainImageViews[m_ImageIndex],
                .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            };
            const VkRenderingInfo postProcessRenderingInfo = {
                .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                .renderArea = {.offset = {0, 0}, .extent = m_SwapChain.swapChainExtent},
                .layerCount = 1,
                .colorAttachmentCount = 1,
                .pColorAttachments = &postProcessAttachment,
            };
            // Left open on purpose: ImGui's Play-mode game UI (ui.Text/ui.Rect) draws directly
            // into this same instance right after, same as it always drew into the forward
            // pass's own rendering before this pass existed.
            vkCmdBeginRendering(cmd, &postProcessRenderingInfo);
            DrawPostProcessFullscreen(cmd, m_SceneColorImage.imageView, m_SwapChain.swapChainExtent);
        }

        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
    }

    void VulkanRHI::Dispatch(uint32_t x, uint32_t y, uint32_t z) {
    }

    void VulkanRHI::UpdateCamera(const glm::mat4& view, const glm::mat4& projection, const glm::vec4& position, const glm::vec3& front) {
        UpdateCascades(view, projection);
        SetCameraBuffer(view, projection, position);
    }

    void VulkanRHI::SetCameraBuffer(const glm::mat4& view, const glm::mat4& projection, const glm::vec4& position) {
        CameraBufferFull cameraBuffer;
        cameraBuffer.view       = view;
        cameraBuffer.projection = projection;
        for (uint32_t i = 0; i < SHADOW_CASCADE_COUNT; i++) {
            cameraBuffer.lightSpaceMatrices[i] = m_LightSpaceMatrices[i];
        }
        cameraBuffer.cascadeSplits = glm::vec4(
            m_CascadeSplits[0], m_CascadeSplits[1], m_CascadeSplits[2], m_EnvironmentExposure);
        cameraBuffer.lightDirection = glm::vec4(-m_DirectionalLight.direction, 0.0f);
        cameraBuffer.cameraPosition = position;

        UploadDynamicBuffer(m_CameraUniformBuffer, &cameraBuffer, sizeof(CameraBufferFull));
    }

    void VulkanRHI::BeginForwardPass() {
        if (!m_FrameStarted) return;
        m_RenderingViewport = false;
        // Renders into m_SceneColorImage, not the swapchain directly, so RenderImGui's Play-mode
        // branch has something to run the post-process pass against before it reaches the
        // swapchain (see the !separatePass branch there).
        BeginScenePass(m_SceneColorImage, m_DepthImage, m_SwapChain.swapChainExtent,
            m_SceneColorImageInitialized ? ResourceState::ShaderRead : ResourceState::Undefined);
        m_SceneColorImageInitialized = true;
    }

    void VulkanRHI::BeginViewportForwardPass() {
        if (!m_FrameStarted || m_ViewportColorImage.image == VK_NULL_HANDLE) return;
        m_RenderingViewport = true;
        BeginScenePass(m_ViewportColorImage, m_ViewportDepthImage, m_ViewportExtent,
            m_ViewportImageInitialized ? ResourceState::ShaderRead : ResourceState::Undefined);
    }

    glm::uvec2 VulkanRHI::GetRenderExtent(bool viewport) const {
        const VkExtent2D extent = viewport ? m_ViewportExtent : m_SwapChain.swapChainExtent;
        return {extent.width, extent.height};
    }

    void VulkanRHI::ResizeViewport(uint32_t width, uint32_t height) {
        if (width == 0 || height == 0) return;

        const bool targetExists = m_ViewportColorImage.image != VK_NULL_HANDLE;
        if (targetExists && m_ViewportExtent.width == width && m_ViewportExtent.height == height) {
            m_PendingViewportExtent = {};
            return;
        }

        if (targetExists) {
            const auto now = std::chrono::steady_clock::now();
            if (m_PendingViewportExtent.width != width || m_PendingViewportExtent.height != height) {
                m_PendingViewportExtent = {width, height};
                m_ViewportResizeRequestedAt = now;
                return;
            }

            constexpr auto resizeDebounce = std::chrono::milliseconds(100);
            if (now - m_ViewportResizeRequestedAt < resizeDebounce) return;
        }

        m_PendingViewportExtent = {};

        vkDeviceWaitIdle(m_Device.logicalDevice);
        if (m_ViewportTextureID != 0) {
            ImGui_ImplVulkan_RemoveTexture(reinterpret_cast<VkDescriptorSet>(m_ViewportTextureID));
            m_ViewportTextureID = 0;
        }
        if (m_PostProcessPreviewTextureID != 0) {
            ImGui_ImplVulkan_RemoveTexture(reinterpret_cast<VkDescriptorSet>(m_PostProcessPreviewTextureID));
            m_PostProcessPreviewTextureID = 0;
        }
        DestroyViewportResources();

        if (!CreateViewportResources(width, height)) {
            OSIRIS_ERROR("Failed to create viewport resources on resize!");
        }
    }

    bool VulkanRHI::CreateViewportResources(uint32_t width, uint32_t height) {
        m_ViewportExtent = {width, height};

        VkImageCreateInfo colorInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = m_SwapChain.swapChainImageFormat,
            .extent = {width, height, 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        };
        VmaAllocationCreateInfo allocationInfo = {.usage = VMA_MEMORY_USAGE_GPU_ONLY};
        VK_CHECK(vmaCreateImage(m_Allocator, &colorInfo, &allocationInfo,
            &m_ViewportColorImage.image, &m_ViewportColorImage.allocation, nullptr));
        m_ViewportColorImage.format = m_SwapChain.swapChainImageFormat;

        VkImageViewCreateInfo colorViewInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = m_ViewportColorImage.image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = m_ViewportColorImage.format,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        };
        VK_CHECK(vkCreateImageView(m_Device.logicalDevice, &colorViewInfo, nullptr,
            &m_ViewportColorImage.imageView));

        VkImageCreateInfo depthInfo = colorInfo;
        depthInfo.format = VK_FORMAT_D32_SFLOAT;
        depthInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        VK_CHECK(vmaCreateImage(m_Allocator, &depthInfo, &allocationInfo,
            &m_ViewportDepthImage.image, &m_ViewportDepthImage.allocation, nullptr));
        m_ViewportDepthImage.format = VK_FORMAT_D32_SFLOAT;

        VkImageViewCreateInfo depthViewInfo = colorViewInfo;
        depthViewInfo.image = m_ViewportDepthImage.image;
        depthViewInfo.format = m_ViewportDepthImage.format;
        depthViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        VK_CHECK(vkCreateImageView(m_Device.logicalDevice, &depthViewInfo, nullptr,
            &m_ViewportDepthImage.imageView));

        const VkDescriptorSet descriptorSet = ImGui_ImplVulkan_AddTexture(
            m_ViewportColorImage.imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_ViewportTextureID = reinterpret_cast<uint64_t>(descriptorSet);
        m_ViewportImageInitialized = false;

        // Post-process debug preview, same size as the viewport, never written to unless
        // GetPostProcessPreviewEnabled() is on (see RenderImGui).
        VK_CHECK(vmaCreateImage(m_Allocator, &colorInfo, &allocationInfo,
            &m_PostProcessPreviewImage.image, &m_PostProcessPreviewImage.allocation, nullptr));
        m_PostProcessPreviewImage.format = m_SwapChain.swapChainImageFormat;

        VkImageViewCreateInfo previewViewInfo = colorViewInfo;
        previewViewInfo.image = m_PostProcessPreviewImage.image;
        VK_CHECK(vkCreateImageView(m_Device.logicalDevice, &previewViewInfo, nullptr,
            &m_PostProcessPreviewImage.imageView));

        const VkDescriptorSet previewDescriptorSet = ImGui_ImplVulkan_AddTexture(
            m_PostProcessPreviewImage.imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_PostProcessPreviewTextureID = reinterpret_cast<uint64_t>(previewDescriptorSet);

        return true;
    }

    void VulkanRHI::DestroyViewportResources() {
        if (m_ViewportColorImage.imageView != VK_NULL_HANDLE)
            vkDestroyImageView(m_Device.logicalDevice, m_ViewportColorImage.imageView, nullptr);
        if (m_ViewportColorImage.image != VK_NULL_HANDLE)
            vmaDestroyImage(m_Allocator, m_ViewportColorImage.image, m_ViewportColorImage.allocation);
        if (m_ViewportDepthImage.imageView != VK_NULL_HANDLE)
            vkDestroyImageView(m_Device.logicalDevice, m_ViewportDepthImage.imageView, nullptr);
        if (m_ViewportDepthImage.image != VK_NULL_HANDLE)
            vmaDestroyImage(m_Allocator, m_ViewportDepthImage.image, m_ViewportDepthImage.allocation);
        if (m_PostProcessPreviewImage.imageView != VK_NULL_HANDLE)
            vkDestroyImageView(m_Device.logicalDevice, m_PostProcessPreviewImage.imageView, nullptr);
        if (m_PostProcessPreviewImage.image != VK_NULL_HANDLE)
            vmaDestroyImage(m_Allocator, m_PostProcessPreviewImage.image, m_PostProcessPreviewImage.allocation);

        m_ViewportColorImage = {};
        m_ViewportDepthImage = {};
        m_ViewportExtent = {};
        m_ViewportImageInitialized = false;
        m_PostProcessPreviewImage = {};
        m_PostProcessPreviewTextureID = 0;
    }

    bool VulkanRHI::CreateSceneColorImage() {
        const VkImageCreateInfo colorInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = m_SwapChain.swapChainImageFormat,
            .extent = {m_SwapChain.swapChainExtent.width, m_SwapChain.swapChainExtent.height, 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        };
        const VmaAllocationCreateInfo allocationInfo = {.usage = VMA_MEMORY_USAGE_GPU_ONLY};
        VK_CHECK(vmaCreateImage(m_Allocator, &colorInfo, &allocationInfo,
            &m_SceneColorImage.image, &m_SceneColorImage.allocation, nullptr));
        m_SceneColorImage.format = m_SwapChain.swapChainImageFormat;

        const VkImageViewCreateInfo colorViewInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = m_SceneColorImage.image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = m_SceneColorImage.format,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        };
        VK_CHECK(vkCreateImageView(m_Device.logicalDevice, &colorViewInfo, nullptr,
            &m_SceneColorImage.imageView));

        m_SceneColorImageInitialized = false;
        return true;
    }

    void VulkanRHI::DestroySceneColorImage() {
        if (m_SceneColorImage.imageView != VK_NULL_HANDLE)
            vkDestroyImageView(m_Device.logicalDevice, m_SceneColorImage.imageView, nullptr);
        if (m_SceneColorImage.image != VK_NULL_HANDLE)
            vmaDestroyImage(m_Allocator, m_SceneColorImage.image, m_SceneColorImage.allocation);
        m_SceneColorImage = {};
        m_SceneColorImageInitialized = false;
    }

    // Assumes rendering is already begun on the destination the caller wants this drawn into.
    // Repoints this frame-in-flight slot's descriptor set at srcView every call rather than
    // keeping a set per source image, Play and the Edit preview never run in the same frame so
    // nothing ever needs both bindings valid at once within a single slot.
    void VulkanRHI::DrawPostProcessFullscreen(VkCommandBuffer cmd, VkImageView srcView, VkExtent2D extent) {
        const VkDescriptorSet descriptorSet = m_PostProcessDescriptorSets[m_CurrentFrame];

        UploadDynamicBuffer(m_PostProcessSettingsBuffer, &m_PostProcessSettings, sizeof(PostProcessSettings));

        const VkDescriptorImageInfo imageInfo = {
            .sampler     = m_PostProcessSampler,
            .imageView   = srcView,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };
        const VkWriteDescriptorSet write = {
            .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet          = descriptorSet,
            .dstBinding      = 0,
            .descriptorCount = 1,
            .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo      = &imageInfo,
        };
        vkUpdateDescriptorSets(m_Device.logicalDevice, 1, &write, 0, nullptr);

        const VkViewport viewport = {
            .x = 0.0f, .y = 0.0f,
            .width = static_cast<float>(extent.width),
            .height = static_cast<float>(extent.height),
            .minDepth = 0.0f, .maxDepth = 1.0f,
        };
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        const VkRect2D scissor = {.offset = {0, 0}, .extent = extent};
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_PostProcessPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_PostProcessPipelineLayout,
            0, 1, &descriptorSet, 0, nullptr);

        // Wall-clock time, not deltaTime-accumulated, purely so film grain animates frame to
        // frame instead of looking like a static dirty lens. Doesn't need to be synced with any
        // other game-time value, it's only ever used to seed the shader's noise hash.
        const float timeSeconds = static_cast<float>(SDL_GetTicks()) / 1000.0f;
        vkCmdPushConstants(cmd, m_PostProcessPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
            0, sizeof(float), &timeSeconds);

        vkCmdDraw(cmd, 3, 1, 0, 0);
    }

    void VulkanRHI::BeginScenePass(VulkanImage& colorImage, VulkanImage& depthImage, VkExtent2D extent,
                                   ResourceState colorInitialState) {
        VkCommandBuffer cmd = m_Frames[m_CurrentFrame].commandBuffer;

        m_RenderGraph.Reset();
        m_RenderGraph.ImportTexture(m_ColorBufferRG, colorImage.image, colorInitialState);
        m_RenderGraph.ImportTexture(m_DepthBufferRG, depthImage.image, ResourceState::Undefined);
        m_RenderGraph.AddPass("ForwardPass", PassType::Graphics)
            .Write({m_ColorBufferRG, ResourceState::ColorWrite})
            .Write({m_DepthBufferRG, ResourceState::DepthWrite})
            .SetExecute(nullptr);
        m_RenderGraph.Compile();
        m_RenderGraph.Execute(cmd);

        VkRenderingAttachmentInfo colorAttachment = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = colorImage.imageView,
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = {.color = {0.0f, 0.0f, 0.0f, 1.0f}},
        };
        VkRenderingAttachmentInfo depthAttachment = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = depthImage.imageView,
            .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .clearValue = {.depthStencil = {1.0f, 0}},
        };
        const VkRenderingInfo renderingInfo = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea = {.offset = {0, 0}, .extent = extent},
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &colorAttachment,
            .pDepthAttachment = &depthAttachment,
        };
        vkCmdBeginRendering(cmd, &renderingInfo);

        VkViewport viewport = {
            .x = 0.0f, .y = 0.0f,
            .width = static_cast<float>(extent.width),
            .height = static_cast<float>(extent.height),
            .minDepth = 0.0f, .maxDepth = 1.0f,
        };
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        VkRect2D scissor = {.offset = {0, 0}, .extent = extent};
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        if (m_EnvironmentLoaded) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_SkyboxPipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                m_SkyboxPipelineLayout, 0, 1, &m_DescriptorSet, 0, nullptr);
            vkCmdPushConstants(cmd, m_SkyboxPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                0, sizeof(float), &m_EnvironmentExposure);
            vkCmdDraw(cmd, 36, 1, 0, 0);
        }

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_ForwardPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            m_ForwardPipelineLayout, 0, 1, &m_DescriptorSet, 0, nullptr);
    }

    void VulkanRHI::BeginShadowPass(uint32_t cascadeIndex) {
    if (!m_FrameStarted) return;

    m_ActiveLightSpaceMatrix = m_LightSpaceMatrices[cascadeIndex];
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
    vkCmdSetDepthBias(cmd, m_ShadowSettings.depthBiasConstant, 0.0f,
        m_ShadowSettings.depthBiasSlope);
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
            .lightSpaceMatrix = m_ActiveLightSpaceMatrix,
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

    void VulkanRHI::BeginSpotShadowPass(uint32_t index) {
        VkCommandBuffer cmd = m_Frames[m_CurrentFrame].commandBuffer;
        m_ActiveLightSpaceMatrix = m_SpotShadowMatrices[index];
        VkImageMemoryBarrier barrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            .image = m_SpotShadowMaps[index].image,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        };
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);

        VkRenderingAttachmentInfo depthAttachment = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = m_SpotShadowMaps[index].imageView,
            .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = {.depthStencil = {1.0f, 0}}
        };

        VkRenderingInfo renderingInfo = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea = {.offset = {0, 0}, .extent = {SPOT_SHADOW_MAP_SIZE, SPOT_SHADOW_MAP_SIZE}},
            .layerCount = 1,
            .colorAttachmentCount = 0,
            .pDepthAttachment = &depthAttachment
        };

        vkCmdBeginRendering(cmd, &renderingInfo);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_ShadowPipeline);
        vkCmdSetDepthBias(cmd, m_ShadowSettings.spotDepthBiasConstant, 0.0f,
            m_ShadowSettings.spotDepthBiasSlope);

        VkViewport viewport = {
            .x = 0, .y = 0,
            .width = (float)SPOT_SHADOW_MAP_SIZE,
            .height = (float)SPOT_SHADOW_MAP_SIZE,
            .minDepth = 0.0f, .maxDepth = 1.0f
        };
        vkCmdSetViewport(cmd, 0, 1, &viewport);

        VkRect2D scissor = { .offset = {0, 0}, .extent = {SPOT_SHADOW_MAP_SIZE, SPOT_SHADOW_MAP_SIZE} };
        vkCmdSetScissor(cmd, 0, 1, &scissor);
    }

    void VulkanRHI::EndSpotShadowPass(uint32_t index) {
        VkCommandBuffer cmd = m_Frames[m_CurrentFrame].commandBuffer;

        vkCmdEndRendering(cmd);

        VkImageMemoryBarrier barrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .image = m_SpotShadowMaps[index].image,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        };
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);
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

        VkPhysicalDeviceFeatures availablePhysicalDeviceFeatures = {};
        vkGetPhysicalDeviceFeatures(m_Device.physicalDevice, &availablePhysicalDeviceFeatures);

        VkPhysicalDeviceProperties physicalDeviceProperties;
        vkGetPhysicalDeviceProperties(m_Device.physicalDevice, &physicalDeviceProperties);

        m_SamplerAnisotropyEnabled = availablePhysicalDeviceFeatures.samplerAnisotropy == VK_TRUE;
        m_MaxSamplerAnisotropy = m_SamplerAnisotropyEnabled
                               ? std::min(8.0f, physicalDeviceProperties.limits.maxSamplerAnisotropy)
                               : 1.0f;
        if (!m_SamplerAnisotropyEnabled) {
            OSIRIS_WARN("Sampler anisotropy is unavailable on the selected GPU");
        }

        VkPhysicalDeviceFeatures physicalDeviceFeatures = {};
        physicalDeviceFeatures.samplerAnisotropy = m_SamplerAnisotropyEnabled ? VK_TRUE : VK_FALSE;
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
        if (!m_Desc.vsync) {
            for (const auto& mode : presentModes) {
                if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
                    presentMode = mode;
                    break;
                }
                if (mode == VK_PRESENT_MODE_IMMEDIATE_KHR) {
                    presentMode = mode;
                }
            }
        }
        OSIRIS_INFO("VSync {}, Vulkan present mode {}",
            m_Desc.vsync ? "enabled" : "disabled",
            presentMode == VK_PRESENT_MODE_FIFO_KHR ? "FIFO"
                : presentMode == VK_PRESENT_MODE_MAILBOX_KHR ? "Mailbox" : "Immediate");

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
        VkShaderModule vertShaderModule = LoadShader(AssetManager::GetEnginePath("shaders/triangle.vert.spv"));
        VkShaderModule fragShaderModule = LoadShader(AssetManager::GetEnginePath("shaders/triangle.frag.spv"));

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
            {
                .binding         = 4,
                .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = MAX_SPOT_SHADOW_CASTERS,
                .stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT,
            },
            {
                .binding         = 5,
                .descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount = 1,
                .stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            },
            {
                .binding         = 6,
                .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = 1,
                .stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT, // IBL environment cubemap (Phase 6D)
            },
            {
                .binding         = 7,
                .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = 1,
                .stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT, // IBL diffuse irradiance cubemap
            },
            {
                .binding         = 8,
                .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = 1,
                .stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT, // IBL specular prefiltered env cubemap
            },
            {
                .binding         = 9,
                .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = 1,
                .stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT, // IBL BRDF integration LUT
            },
        };

        VkDescriptorSetLayoutCreateInfo frameLayoutInfo = {
            .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = 10,
            .pBindings    = frameBindings,
        };
        VK_CHECK(vkCreateDescriptorSetLayout(m_Device.logicalDevice, &frameLayoutInfo, nullptr, &m_FrameDescriptorLayout));

        VkDescriptorSetLayoutBinding textureBinding[] = {
            {
                .binding         = 0,
               .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
               .descriptorCount = 1,
               .stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT,
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
            {
                .binding         = 4,
                .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = 1,
                .stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT,
            }
        };
        VkDescriptorSetLayoutCreateInfo materialLayoutInfo = {
            .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = 5,
            .pBindings    = textureBinding,
        };
        VK_CHECK(vkCreateDescriptorSetLayout(m_Device.logicalDevice, &materialLayoutInfo, nullptr, &m_MaterialDescriptorLayout));

        const VkDescriptorSetLayoutBinding postProcessBindings[] = {
            {
                .binding         = 0,
                .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = 1,
                .stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT,
            },
            {
                .binding         = 1,
                .descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount = 1,
                .stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT,
            },
        };
        const VkDescriptorSetLayoutCreateInfo postProcessLayoutInfo = {
            .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = 2,
            .pBindings    = postProcessBindings,
        };
        VK_CHECK(vkCreateDescriptorSetLayout(m_Device.logicalDevice, &postProcessLayoutInfo, nullptr, &m_PostProcessDescriptorLayout));

        OSIRIS_INFO("Descriptor set layouts created!");
        return true;
    }

    bool VulkanRHI::CreateDescriptorPool() {
        // UNIFORM_BUFFER must cover every UBO binding across every set this pool ever allocates
        // (frame set: 2, post-process: 1 per frame-in-flight slot); kept with headroom since an
        // exact-fit count here is what silently broke post-processing (VK_CHECK only logs a
        // failed vkAllocateDescriptorSets, it doesn't surface it hard).
        VkDescriptorPoolSize poolSizes[] = {
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         16 },
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 5003  },
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          32 }, // IBL precompute (Phase 6D)
        };
        const VkDescriptorPoolCreateInfo descriptorPoolInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = 5003,
            .poolSizeCount = 3,
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

        BufferDesc spotLightUniformBufferDesc = {
            .size       = sizeof(SpotLightBufferFull),
            .usage      = BufferUsage::Uniform,
            .cpuVisible = true,
        };

        m_SpotLightUniformBuffer = CreateBuffer(spotLightUniformBufferDesc);

        VkDescriptorBufferInfo spotLightBufferInfo = {
            .buffer = m_Buffers.at(m_SpotLightUniformBuffer.id).buffer,
            .offset = 0,
            .range  = sizeof(SpotLightBufferFull),
        };

        const VkWriteDescriptorSet writeDescriptorSets[] = {
            {
                .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet          = m_DescriptorSet,
                .dstBinding      = 0,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .pBufferInfo     = &bufferInfo,
            },
            {
                .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet          = m_DescriptorSet,
                .dstBinding      = 5,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .pBufferInfo     = &spotLightBufferInfo,
            },
        };

        vkUpdateDescriptorSets(m_Device.logicalDevice, 2, writeDescriptorSets, 0, nullptr);

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

            VK_CHECK(vkCreateSampler(m_Device.logicalDevice, &samplerInfo, nullptr, &map.sampler));
        }

        VkImageCreateInfo spotImageCreateInfo = imageCreateInfo;
        spotImageCreateInfo.extent = {SPOT_SHADOW_MAP_SIZE, SPOT_SHADOW_MAP_SIZE, 1};

        for (auto& map : m_SpotShadowMaps) {
            map.format = VK_FORMAT_D32_SFLOAT;
            VK_CHECK(vmaCreateImage(m_Allocator, &spotImageCreateInfo, &allocationCreateInfo, &map.image, &map.allocation, nullptr));

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
                OSIRIS_ERROR("Failed to create image view for spot shadow map!");
                return false;
            }

            VK_CHECK(vkCreateSampler(m_Device.logicalDevice, &samplerInfo, nullptr, &map.sampler));
        }

        return true;
    }

    bool VulkanRHI::GenerateBRDFLUT() {
        // Descriptor set layout: single storage image, written once by the
        // compute shader below.
        VkDescriptorSetLayoutBinding lutBinding = {
            .binding         = 0,
            .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = 1,
            .stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT,
        };
        VkDescriptorSetLayoutCreateInfo layoutInfo = {
            .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = 1,
            .pBindings    = &lutBinding,
        };
        VK_CHECK(vkCreateDescriptorSetLayout(m_Device.logicalDevice, &layoutInfo, nullptr, &m_BRDFLutComputeLayout));

        // Image.
        VkImageCreateInfo imageCreateInfo = {
            .sType     = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D,
            .format    = VK_FORMAT_R16G16_SFLOAT,
            .extent    = {BRDF_LUT_SIZE, BRDF_LUT_SIZE, 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples   = VK_SAMPLE_COUNT_1_BIT,
            .usage     = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        };
        VmaAllocationCreateInfo allocationCreateInfo = {
            .usage = VMA_MEMORY_USAGE_GPU_ONLY,
        };
        m_BRDFLut.format = VK_FORMAT_R16G16_SFLOAT;
        VK_CHECK(vmaCreateImage(m_Allocator, &imageCreateInfo, &allocationCreateInfo,
            &m_BRDFLut.image, &m_BRDFLut.allocation, nullptr));

        VkImageViewCreateInfo viewCreateInfo = {
            .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image    = m_BRDFLut.image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format   = VK_FORMAT_R16G16_SFLOAT,
            .subresourceRange = {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel   = 0,
                .levelCount     = 1,
                .baseArrayLayer = 0,
                .layerCount     = 1,
            },
        };
        VK_CHECK(vkCreateImageView(m_Device.logicalDevice, &viewCreateInfo, nullptr, &m_BRDFLut.imageView));

        VkSamplerCreateInfo samplerInfo = {
            .sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter    = VK_FILTER_LINEAR,
            .minFilter    = VK_FILTER_LINEAR,
            .mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR,
            .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .borderColor  = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
        };
        VK_CHECK(vkCreateSampler(m_Device.logicalDevice, &samplerInfo, nullptr, &m_BRDFLut.sampler));

        // Descriptor set.
        VkDescriptorSet lutSet;
        VkDescriptorSetAllocateInfo setAllocInfo = {
            .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool     = m_DescriptorPool,
            .descriptorSetCount = 1,
            .pSetLayouts        = &m_BRDFLutComputeLayout,
        };
        VK_CHECK(vkAllocateDescriptorSets(m_Device.logicalDevice, &setAllocInfo, &lutSet));

        VkDescriptorImageInfo lutImageInfo = {
            .imageView   = m_BRDFLut.imageView,
            .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
        };
        VkWriteDescriptorSet lutWrite = {
            .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet          = lutSet,
            .dstBinding      = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .pImageInfo      = &lutImageInfo,
        };
        vkUpdateDescriptorSets(m_Device.logicalDevice, 1, &lutWrite, 0, nullptr);

        // Pipeline.
        VkPipeline pipeline = m_PipelineManager->GetOrCreateCompute({
            .computeShader   = AssetManager::GetEnginePath("shaders/brdf_lut.comp.spv"),
            .setLayoutCount  = 1,
            .pSetLayouts     = &m_BRDFLutComputeLayout,
        });
        VkPipelineLayout pipelineLayout = m_PipelineManager->GetLayout(pipeline);

        // Dispatch.
        VkCommandBuffer cmd = BeginOneTimeCommands();

        VkImageMemoryBarrier toGeneral = {
            .sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_NONE,
            .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
            .oldLayout     = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout     = VK_IMAGE_LAYOUT_GENERAL,
            .image         = m_BRDFLut.image,
            .subresourceRange = {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel   = 0,
                .levelCount     = 1,
                .baseArrayLayer = 0,
                .layerCount     = 1,
            },
        };
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &toGeneral);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &lutSet, 0, nullptr);
        vkCmdDispatch(cmd, (BRDF_LUT_SIZE + 7) / 8, (BRDF_LUT_SIZE + 7) / 8, 1);

        VkImageMemoryBarrier toShaderRead = {
            .sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .oldLayout     = VK_IMAGE_LAYOUT_GENERAL,
            .newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .image         = m_BRDFLut.image,
            .subresourceRange = {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel   = 0,
                .levelCount     = 1,
                .baseArrayLayer = 0,
                .layerCount     = 1,
            },
        };
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &toShaderRead);

        EndOneTimeCommands(cmd);

        // Frame set binding 9: sampled by triangle.frag's specular IBL term.
        VkDescriptorImageInfo lutSampledInfo = {
            .sampler     = m_BRDFLut.sampler,
            .imageView   = m_BRDFLut.imageView,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };
        VkWriteDescriptorSet lutSampledWrite = {
            .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet          = m_DescriptorSet,
            .dstBinding      = 9,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo      = &lutSampledInfo,
        };
        vkUpdateDescriptorSets(m_Device.logicalDevice, 1, &lutSampledWrite, 0, nullptr);

        OSIRIS_INFO("BRDF LUT generated ({}x{})", BRDF_LUT_SIZE, BRDF_LUT_SIZE);
        return true;
    }

    bool VulkanRHI::CreateDefaultEnvironmentCubemap() {
        const VmaAllocationCreateInfo allocationCreateInfo = {
            .usage = VMA_MEMORY_USAGE_GPU_ONLY,
        };

        VkImageCreateInfo imageInfo = {
            .sType       = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .flags       = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
            .imageType   = VK_IMAGE_TYPE_2D,
            .format      = VK_FORMAT_R32G32B32A32_SFLOAT,
            .extent      = {1, 1, 1},
            .mipLevels   = 1,
            .arrayLayers = 6,
            .samples     = VK_SAMPLE_COUNT_1_BIT,
            .usage       = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        };
        m_DefaultEnvironmentCubemap.format = VK_FORMAT_R32G32B32A32_SFLOAT;
        m_DefaultEnvironmentCubemap.size   = 1;
        VK_CHECK(vmaCreateImage(m_Allocator, &imageInfo, &allocationCreateInfo,
            &m_DefaultEnvironmentCubemap.image, &m_DefaultEnvironmentCubemap.allocation, nullptr));

        const float pixels[6 * 4] = {}; // 6 faces x RGBA, zero-initialized = black

        BufferDesc stagingDesc = {
            .size       = sizeof(pixels),
            .usage      = BufferUsage::Transfer,
            .cpuVisible = true,
        };
        BufferHandle stagingHandle = CreateBuffer(stagingDesc);
        memcpy(m_Buffers[stagingHandle.id].allocationInfo.pMappedData, pixels, sizeof(pixels));

        VkCommandBuffer cmd = BeginOneTimeCommands();

        VkImageMemoryBarrier toDst = {
            .sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_NONE,
            .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout     = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .image         = m_DefaultEnvironmentCubemap.image,
            .subresourceRange = {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel   = 0,
                .levelCount     = 1,
                .baseArrayLayer = 0,
                .layerCount     = 6,
            },
        };
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &toDst);

        VkBufferImageCopy copyRegion = {
            .imageSubresource = {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel       = 0,
                .baseArrayLayer = 0,
                .layerCount     = 6,
            },
            .imageExtent = {1, 1, 1},
        };
        vkCmdCopyBufferToImage(cmd, m_Buffers[stagingHandle.id].buffer, m_DefaultEnvironmentCubemap.image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

        VkImageMemoryBarrier toShaderRead = {
            .sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .image         = m_DefaultEnvironmentCubemap.image,
            .subresourceRange = {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel   = 0,
                .levelCount     = 1,
                .baseArrayLayer = 0,
                .layerCount     = 6,
            },
        };
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &toShaderRead);

        EndOneTimeCommands(cmd);
        DestroyBuffer(stagingHandle);

        VkImageViewCreateInfo viewInfo = {
            .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image    = m_DefaultEnvironmentCubemap.image,
            .viewType = VK_IMAGE_VIEW_TYPE_CUBE,
            .format   = VK_FORMAT_R32G32B32A32_SFLOAT,
            .subresourceRange = {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel   = 0,
                .levelCount     = 1,
                .baseArrayLayer = 0,
                .layerCount     = 6,
            },
        };
        VK_CHECK(vkCreateImageView(m_Device.logicalDevice, &viewInfo, nullptr, &m_DefaultEnvironmentCubemap.sampledView));

        VkSamplerCreateInfo samplerInfo = {
            .sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter    = VK_FILTER_NEAREST,
            .minFilter    = VK_FILTER_NEAREST,
            .mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST,
            .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .borderColor  = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK,
        };
        VK_CHECK(vkCreateSampler(m_Device.logicalDevice, &samplerInfo, nullptr, &m_DefaultEnvironmentCubemap.sampler));

        VkDescriptorImageInfo defaultInfo = {
            .sampler     = m_DefaultEnvironmentCubemap.sampler,
            .imageView   = m_DefaultEnvironmentCubemap.sampledView,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };
        VkWriteDescriptorSet defaultWrites[3];
        for (uint32_t i = 0; i < 3; i++) {
            defaultWrites[i] = VkWriteDescriptorSet{
                .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet          = m_DescriptorSet,
                .dstBinding      = 6 + i,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo      = &defaultInfo,
            };
        }
        vkUpdateDescriptorSets(m_Device.logicalDevice, 3, defaultWrites, 0, nullptr);

        return true;
    }

    bool VulkanRHI::LoadEnvironmentMap(const float* pixels, uint32_t width, uint32_t height) {
        if (pixels == nullptr || width == 0 || height == 0) {
            OSIRIS_ERROR("LoadEnvironmentMap: invalid pixel data");
            return false;
        }

        // Diagnostic: HDR radiance values have no fixed scale, so there's no
        // universally "correct" exposure — log min/max/avg luminance once so
        // the UI's exposure slider default/range can be sanity-checked against
        // real data instead of guessed.
        {
            float minLum = FLT_MAX, maxLum = 0.0f, sumLum = 0.0f;
            const uint64_t pixelCount = static_cast<uint64_t>(width) * height;
            for (uint64_t i = 0; i < pixelCount; i++) {
                const float* p = pixels + i * 4;
                float lum = 0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2];
                minLum = std::min(minLum, lum);
                maxLum = std::max(maxLum, lum);
                sumLum += lum;
            }
            OSIRIS_INFO("LoadEnvironmentMap: luminance min={:.4f} max={:.4f} avg={:.4f}",
                minLum, maxLum, sumLum / static_cast<float>(pixelCount));
        }

        const VmaAllocationCreateInfo allocationCreateInfo = {
            .usage = VMA_MEMORY_USAGE_GPU_ONLY,
        };

        // 1. Transient equirect image — only exists to seed the cubemap
        // conversion below, destroyed at the end of this function.
        VkImageCreateInfo equirectImageInfo = {
            .sType       = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType   = VK_IMAGE_TYPE_2D,
            .format      = VK_FORMAT_R32G32B32A32_SFLOAT,
            .extent      = {width, height, 1},
            .mipLevels   = 1,
            .arrayLayers = 1,
            .samples     = VK_SAMPLE_COUNT_1_BIT,
            .usage       = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        };

        VulkanImage equirectImage;
        equirectImage.format = VK_FORMAT_R32G32B32A32_SFLOAT;
        VK_CHECK(vmaCreateImage(m_Allocator, &equirectImageInfo, &allocationCreateInfo,
            &equirectImage.image, &equirectImage.allocation, nullptr));

        TransitionImageLayout(equirectImage.image,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        const uint64_t dataSize = static_cast<uint64_t>(width) * height * 4 * sizeof(float);
        BufferDesc stagingDesc = {
            .size       = dataSize,
            .usage      = BufferUsage::Transfer,
            .cpuVisible = true,
        };
        BufferHandle stagingHandle = CreateBuffer(stagingDesc);
        memcpy(m_Buffers[stagingHandle.id].allocationInfo.pMappedData, pixels, dataSize);

        VkCommandBuffer copyCmd = BeginOneTimeCommands();
        VkBufferImageCopy copyRegion = {
            .imageSubresource = {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel       = 0,
                .baseArrayLayer = 0,
                .layerCount     = 1,
            },
            .imageExtent = {width, height, 1},
        };
        vkCmdCopyBufferToImage(copyCmd, m_Buffers[stagingHandle.id].buffer, equirectImage.image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);
        EndOneTimeCommands(copyCmd);
        DestroyBuffer(stagingHandle);

        TransitionImageLayout(equirectImage.image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        VkImageViewCreateInfo equirectViewInfo = {
            .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image    = equirectImage.image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format   = VK_FORMAT_R32G32B32A32_SFLOAT,
            .subresourceRange = {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel   = 0,
                .levelCount     = 1,
                .baseArrayLayer = 0,
                .layerCount     = 1,
            },
        };
        VK_CHECK(vkCreateImageView(m_Device.logicalDevice, &equirectViewInfo, nullptr, &equirectImage.imageView));

        VkSamplerCreateInfo equirectSamplerInfo = {
            .sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter    = VK_FILTER_LINEAR,
            .minFilter    = VK_FILTER_LINEAR,
            .mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR,
            .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,        // longitude wraps
            .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, // latitude doesn't
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .borderColor  = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK,
        };
        VK_CHECK(vkCreateSampler(m_Device.logicalDevice, &equirectSamplerInfo, nullptr, &equirectImage.sampler));

        // 2. Cubemap image: one VkImage, two views (2D_ARRAY for compute
        // writes, CUBE for later samplerCube reads).
        VkImageCreateInfo cubemapImageInfo = {
            .sType       = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .flags       = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
            .imageType   = VK_IMAGE_TYPE_2D,
            .format      = VK_FORMAT_R16G16B16A16_SFLOAT,
            .extent      = {ENV_CUBEMAP_SIZE, ENV_CUBEMAP_SIZE, 1},
            .mipLevels   = 1,
            .arrayLayers = 6,
            .samples     = VK_SAMPLE_COUNT_1_BIT,
            .usage       = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        };
        m_EnvironmentCubemap.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        m_EnvironmentCubemap.size   = ENV_CUBEMAP_SIZE;
        VK_CHECK(vmaCreateImage(m_Allocator, &cubemapImageInfo, &allocationCreateInfo,
            &m_EnvironmentCubemap.image, &m_EnvironmentCubemap.allocation, nullptr));

        VkImageViewCreateInfo storageViewInfo = {
            .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image    = m_EnvironmentCubemap.image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY,
            .format   = VK_FORMAT_R16G16B16A16_SFLOAT,
            .subresourceRange = {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel   = 0,
                .levelCount     = 1,
                .baseArrayLayer = 0,
                .layerCount     = 6,
            },
        };
        VK_CHECK(vkCreateImageView(m_Device.logicalDevice, &storageViewInfo, nullptr, &m_EnvironmentCubemap.storageView));

        VkImageViewCreateInfo sampledViewInfo = storageViewInfo;
        sampledViewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
        VK_CHECK(vkCreateImageView(m_Device.logicalDevice, &sampledViewInfo, nullptr, &m_EnvironmentCubemap.sampledView));

        VkSamplerCreateInfo cubemapSamplerInfo = {
            .sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter    = VK_FILTER_LINEAR,
            .minFilter    = VK_FILTER_LINEAR,
            .mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR,
            .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .borderColor  = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK,
        };
        VK_CHECK(vkCreateSampler(m_Device.logicalDevice, &cubemapSamplerInfo, nullptr, &m_EnvironmentCubemap.sampler));

        // 3. Descriptor set layout + set for the conversion compute pass.
        VkDescriptorSetLayoutBinding bindings[] = {
            {
                .binding         = 0,
                .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = 1,
                .stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT,
            },
            {
                .binding         = 1,
                .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                .descriptorCount = 1,
                .stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT,
            },
        };
        VkDescriptorSetLayoutCreateInfo convertLayoutInfo = {
            .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = 2,
            .pBindings    = bindings,
        };
        VK_CHECK(vkCreateDescriptorSetLayout(m_Device.logicalDevice, &convertLayoutInfo, nullptr, &m_EquirectToCubemapLayout));

        VkDescriptorSet convertSet;
        VkDescriptorSetAllocateInfo convertSetAllocInfo = {
            .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool     = m_DescriptorPool,
            .descriptorSetCount = 1,
            .pSetLayouts        = &m_EquirectToCubemapLayout,
        };
        VK_CHECK(vkAllocateDescriptorSets(m_Device.logicalDevice, &convertSetAllocInfo, &convertSet));

        VkDescriptorImageInfo equirectDescInfo = {
            .sampler     = equirectImage.sampler,
            .imageView   = equirectImage.imageView,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };
        VkDescriptorImageInfo cubemapDescInfo = {
            .imageView   = m_EnvironmentCubemap.storageView,
            .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
        };
        VkWriteDescriptorSet writes[] = {
            {
                .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet          = convertSet,
                .dstBinding      = 0,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo      = &equirectDescInfo,
            },
            {
                .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet          = convertSet,
                .dstBinding      = 1,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                .pImageInfo      = &cubemapDescInfo,
            },
        };
        vkUpdateDescriptorSets(m_Device.logicalDevice, 2, writes, 0, nullptr);

        VkPipeline pipeline = m_PipelineManager->GetOrCreateCompute({
            .computeShader  = AssetManager::GetEnginePath("shaders/equirect_to_cubemap.comp.spv"),
            .setLayoutCount = 1,
            .pSetLayouts    = &m_EquirectToCubemapLayout,
        });
        VkPipelineLayout pipelineLayout = m_PipelineManager->GetLayout(pipeline);

        // 4. Dispatch: one invocation per cubemap texel, all 6 faces at once
        // (z dimension of the dispatch = face index).
        VkCommandBuffer cmd = BeginOneTimeCommands();

        VkImageMemoryBarrier toGeneral = {
            .sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_NONE,
            .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
            .oldLayout     = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout     = VK_IMAGE_LAYOUT_GENERAL,
            .image         = m_EnvironmentCubemap.image,
            .subresourceRange = {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel   = 0,
                .levelCount     = 1,
                .baseArrayLayer = 0,
                .layerCount     = 6,
            },
        };
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &toGeneral);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &convertSet, 0, nullptr);
        vkCmdDispatch(cmd, (ENV_CUBEMAP_SIZE + 7) / 8, (ENV_CUBEMAP_SIZE + 7) / 8, 6);

        VkImageMemoryBarrier toShaderRead = {
            .sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .oldLayout     = VK_IMAGE_LAYOUT_GENERAL,
            .newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .image         = m_EnvironmentCubemap.image,
            .subresourceRange = {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel   = 0,
                .levelCount     = 1,
                .baseArrayLayer = 0,
                .layerCount     = 6,
            },
        };
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &toShaderRead);

        EndOneTimeCommands(cmd);

        // The equirect staging image was only needed for the conversion above.
        vkDestroySampler(m_Device.logicalDevice, equirectImage.sampler, nullptr);
        vkDestroyImageView(m_Device.logicalDevice, equirectImage.imageView, nullptr);
        vmaDestroyImage(m_Allocator, equirectImage.image, equirectImage.allocation);

        if (!ConvolveIrradiance()) {
            OSIRIS_ERROR("Failed to convolve diffuse irradiance");
            return false;
        }

        if (!PrefilterEnvironment()) {
            OSIRIS_ERROR("Failed to prefilter specular environment");
            return false;
        }

        // Frame set bindings 6-8: environment cubemap (skybox), diffuse
        // irradiance and specular prefiltered cubemaps (IBL lighting term).
        VkDescriptorImageInfo envDescInfo = {
            .sampler     = m_EnvironmentCubemap.sampler,
            .imageView   = m_EnvironmentCubemap.sampledView,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };
        VkDescriptorImageInfo irradianceDescInfo = {
            .sampler     = m_IrradianceCubemap.sampler,
            .imageView   = m_IrradianceCubemap.sampledView,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };
        VkDescriptorImageInfo prefilteredDescInfo = {
            .sampler     = m_PrefilteredCubemap.sampler,
            .imageView   = m_PrefilteredCubemap.sampledView,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };
        VkWriteDescriptorSet envWrites[] = {
            {
                .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet          = m_DescriptorSet,
                .dstBinding      = 6,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo      = &envDescInfo,
            },
            {
                .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet          = m_DescriptorSet,
                .dstBinding      = 7,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo      = &irradianceDescInfo,
            },
            {
                .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet          = m_DescriptorSet,
                .dstBinding      = 8,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo      = &prefilteredDescInfo,
            },
        };
        vkUpdateDescriptorSets(m_Device.logicalDevice, 3, envWrites, 0, nullptr);
        m_EnvironmentLoaded = true;

        OSIRIS_INFO("Environment cubemap generated from equirect ({}x{} -> {}x{} x6)",
            width, height, ENV_CUBEMAP_SIZE, ENV_CUBEMAP_SIZE);
        return true;
    }

    bool VulkanRHI::ConvolveIrradiance() {
        const VmaAllocationCreateInfo allocationCreateInfo = {
            .usage = VMA_MEMORY_USAGE_GPU_ONLY,
        };

        VkImageCreateInfo irradianceImageInfo = {
            .sType       = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .flags       = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
            .imageType   = VK_IMAGE_TYPE_2D,
            .format      = VK_FORMAT_R16G16B16A16_SFLOAT,
            .extent      = {IRRADIANCE_CUBEMAP_SIZE, IRRADIANCE_CUBEMAP_SIZE, 1},
            .mipLevels   = 1,
            .arrayLayers = 6,
            .samples     = VK_SAMPLE_COUNT_1_BIT,
            .usage       = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        };
        m_IrradianceCubemap.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        m_IrradianceCubemap.size   = IRRADIANCE_CUBEMAP_SIZE;
        VK_CHECK(vmaCreateImage(m_Allocator, &irradianceImageInfo, &allocationCreateInfo,
            &m_IrradianceCubemap.image, &m_IrradianceCubemap.allocation, nullptr));

        VkImageViewCreateInfo storageViewInfo = {
            .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image    = m_IrradianceCubemap.image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY,
            .format   = VK_FORMAT_R16G16B16A16_SFLOAT,
            .subresourceRange = {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel   = 0,
                .levelCount     = 1,
                .baseArrayLayer = 0,
                .layerCount     = 6,
            },
        };
        VK_CHECK(vkCreateImageView(m_Device.logicalDevice, &storageViewInfo, nullptr, &m_IrradianceCubemap.storageView));

        VkImageViewCreateInfo sampledViewInfo = storageViewInfo;
        sampledViewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
        VK_CHECK(vkCreateImageView(m_Device.logicalDevice, &sampledViewInfo, nullptr, &m_IrradianceCubemap.sampledView));

        VkSamplerCreateInfo samplerInfo = {
            .sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter    = VK_FILTER_LINEAR,
            .minFilter    = VK_FILTER_LINEAR,
            .mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR,
            .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .borderColor  = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK,
        };
        VK_CHECK(vkCreateSampler(m_Device.logicalDevice, &samplerInfo, nullptr, &m_IrradianceCubemap.sampler));

        // Descriptor set layout + set: binding 0 reads the environment cubemap
        // built by LoadEnvironmentMap, binding 1 writes the irradiance cubemap.
        VkDescriptorSetLayoutBinding bindings[] = {
            {
                .binding         = 0,
                .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = 1,
                .stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT,
            },
            {
                .binding         = 1,
                .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                .descriptorCount = 1,
                .stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT,
            },
        };
        VkDescriptorSetLayoutCreateInfo layoutInfo = {
            .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = 2,
            .pBindings    = bindings,
        };
        VK_CHECK(vkCreateDescriptorSetLayout(m_Device.logicalDevice, &layoutInfo, nullptr, &m_IrradianceConvolveLayout));

        VkDescriptorSet convolveSet;
        VkDescriptorSetAllocateInfo setAllocInfo = {
            .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool     = m_DescriptorPool,
            .descriptorSetCount = 1,
            .pSetLayouts        = &m_IrradianceConvolveLayout,
        };
        VK_CHECK(vkAllocateDescriptorSets(m_Device.logicalDevice, &setAllocInfo, &convolveSet));

        VkDescriptorImageInfo envInfo = {
            .sampler     = m_EnvironmentCubemap.sampler,
            .imageView   = m_EnvironmentCubemap.sampledView,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };
        VkDescriptorImageInfo irrInfo = {
            .imageView   = m_IrradianceCubemap.storageView,
            .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
        };
        VkWriteDescriptorSet writes[] = {
            {
                .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet          = convolveSet,
                .dstBinding      = 0,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo      = &envInfo,
            },
            {
                .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet          = convolveSet,
                .dstBinding      = 1,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                .pImageInfo      = &irrInfo,
            },
        };
        vkUpdateDescriptorSets(m_Device.logicalDevice, 2, writes, 0, nullptr);

        VkPipeline pipeline = m_PipelineManager->GetOrCreateCompute({
            .computeShader  = AssetManager::GetEnginePath("shaders/irradiance_convolve.comp.spv"),
            .setLayoutCount = 1,
            .pSetLayouts    = &m_IrradianceConvolveLayout,
        });
        VkPipelineLayout pipelineLayout = m_PipelineManager->GetLayout(pipeline);

        VkCommandBuffer cmd = BeginOneTimeCommands();

        VkImageMemoryBarrier toGeneral = {
            .sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_NONE,
            .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
            .oldLayout     = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout     = VK_IMAGE_LAYOUT_GENERAL,
            .image         = m_IrradianceCubemap.image,
            .subresourceRange = {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel   = 0,
                .levelCount     = 1,
                .baseArrayLayer = 0,
                .layerCount     = 6,
            },
        };
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &toGeneral);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &convolveSet, 0, nullptr);
        vkCmdDispatch(cmd, (IRRADIANCE_CUBEMAP_SIZE + 7) / 8, (IRRADIANCE_CUBEMAP_SIZE + 7) / 8, 6);

        VkImageMemoryBarrier toShaderRead = {
            .sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .oldLayout     = VK_IMAGE_LAYOUT_GENERAL,
            .newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .image         = m_IrradianceCubemap.image,
            .subresourceRange = {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel   = 0,
                .levelCount     = 1,
                .baseArrayLayer = 0,
                .layerCount     = 6,
            },
        };
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &toShaderRead);

        EndOneTimeCommands(cmd);

        OSIRIS_INFO("Diffuse irradiance cubemap generated ({}x{} x6)",
            IRRADIANCE_CUBEMAP_SIZE, IRRADIANCE_CUBEMAP_SIZE);
        return true;
    }

    bool VulkanRHI::PrefilterEnvironment() {
        const VmaAllocationCreateInfo allocationCreateInfo = {
            .usage = VMA_MEMORY_USAGE_GPU_ONLY,
        };

        VkImageCreateInfo prefilterImageInfo = {
            .sType       = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .flags       = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
            .imageType   = VK_IMAGE_TYPE_2D,
            .format      = VK_FORMAT_R16G16B16A16_SFLOAT,
            .extent      = {PREFILTER_BASE_SIZE, PREFILTER_BASE_SIZE, 1},
            .mipLevels   = PREFILTER_MIP_COUNT,
            .arrayLayers = 6,
            .samples     = VK_SAMPLE_COUNT_1_BIT,
            .usage       = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        };
        m_PrefilteredCubemap.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        m_PrefilteredCubemap.size   = PREFILTER_BASE_SIZE;
        VK_CHECK(vmaCreateImage(m_Allocator, &prefilterImageInfo, &allocationCreateInfo,
            &m_PrefilteredCubemap.image, &m_PrefilteredCubemap.allocation, nullptr));

        // One storage view per mip (baseMipLevel pinned, levelCount=1) so each
        // roughness level's compute dispatch can imageStore into just that mip.
        for (uint32_t mip = 0; mip < PREFILTER_MIP_COUNT; mip++) {
            VkImageViewCreateInfo mipViewInfo = {
                .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image    = m_PrefilteredCubemap.image,
                .viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY,
                .format   = VK_FORMAT_R16G16B16A16_SFLOAT,
                .subresourceRange = {
                    .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel   = mip,
                    .levelCount     = 1,
                    .baseArrayLayer = 0,
                    .layerCount     = 6,
                },
            };
            VK_CHECK(vkCreateImageView(m_Device.logicalDevice, &mipViewInfo, nullptr, &m_PrefilterMipViews[mip]));
        }

        // Final sampled view spans every mip, for textureLod(roughness) reads
        // once this is wired into the forward pass (task 6).
        VkImageViewCreateInfo sampledViewInfo = {
            .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image    = m_PrefilteredCubemap.image,
            .viewType = VK_IMAGE_VIEW_TYPE_CUBE,
            .format   = VK_FORMAT_R16G16B16A16_SFLOAT,
            .subresourceRange = {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel   = 0,
                .levelCount     = PREFILTER_MIP_COUNT,
                .baseArrayLayer = 0,
                .layerCount     = 6,
            },
        };
        VK_CHECK(vkCreateImageView(m_Device.logicalDevice, &sampledViewInfo, nullptr, &m_PrefilteredCubemap.sampledView));

        VkSamplerCreateInfo samplerInfo = {
            .sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter    = VK_FILTER_LINEAR,
            .minFilter    = VK_FILTER_LINEAR,
            .mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR,
            .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .maxLod       = static_cast<float>(PREFILTER_MIP_COUNT - 1),
            .borderColor  = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK,
        };
        VK_CHECK(vkCreateSampler(m_Device.logicalDevice, &samplerInfo, nullptr, &m_PrefilteredCubemap.sampler));

        // Descriptor set layout is shared across every mip dispatch below —
        // only the storage image binding's target view changes per iteration.
        VkDescriptorSetLayoutBinding bindings[] = {
            {
                .binding         = 0,
                .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = 1,
                .stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT,
            },
            {
                .binding         = 1,
                .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                .descriptorCount = 1,
                .stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT,
            },
        };
        VkDescriptorSetLayoutCreateInfo layoutInfo = {
            .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = 2,
            .pBindings    = bindings,
        };
        VK_CHECK(vkCreateDescriptorSetLayout(m_Device.logicalDevice, &layoutInfo, nullptr, &m_PrefilterConvolveLayout));

        VkPipeline pipeline = m_PipelineManager->GetOrCreateCompute({
            .computeShader      = AssetManager::GetEnginePath("shaders/prefilter_env.comp.spv"),
            .setLayoutCount     = 1,
            .pSetLayouts        = &m_PrefilterConvolveLayout,
            .pushConstantSize   = sizeof(float),
            .pushConstantStages = VK_SHADER_STAGE_COMPUTE_BIT,
        });
        VkPipelineLayout pipelineLayout = m_PipelineManager->GetLayout(pipeline);

        for (uint32_t mip = 0; mip < PREFILTER_MIP_COUNT; mip++) {
            const uint32_t mipSize = PREFILTER_BASE_SIZE >> mip;
            const float roughness = static_cast<float>(mip) / static_cast<float>(PREFILTER_MIP_COUNT - 1);

            VkDescriptorSet mipSet;
            VkDescriptorSetAllocateInfo setAllocInfo = {
                .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                .descriptorPool     = m_DescriptorPool,
                .descriptorSetCount = 1,
                .pSetLayouts        = &m_PrefilterConvolveLayout,
            };
            VK_CHECK(vkAllocateDescriptorSets(m_Device.logicalDevice, &setAllocInfo, &mipSet));

            VkDescriptorImageInfo envInfo = {
                .sampler     = m_EnvironmentCubemap.sampler,
                .imageView   = m_EnvironmentCubemap.sampledView,
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            };
            VkDescriptorImageInfo mipInfo = {
                .imageView   = m_PrefilterMipViews[mip],
                .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
            };
            VkWriteDescriptorSet writes[] = {
                {
                    .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet          = mipSet,
                    .dstBinding      = 0,
                    .dstArrayElement = 0,
                    .descriptorCount = 1,
                    .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .pImageInfo      = &envInfo,
                },
                {
                    .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet          = mipSet,
                    .dstBinding      = 1,
                    .dstArrayElement = 0,
                    .descriptorCount = 1,
                    .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                    .pImageInfo      = &mipInfo,
                },
            };
            vkUpdateDescriptorSets(m_Device.logicalDevice, 2, writes, 0, nullptr);

            VkCommandBuffer cmd = BeginOneTimeCommands();

            VkImageMemoryBarrier toGeneral = {
                .sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_NONE,
                .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                .oldLayout     = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout     = VK_IMAGE_LAYOUT_GENERAL,
                .image         = m_PrefilteredCubemap.image,
                .subresourceRange = {
                    .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel   = mip,
                    .levelCount     = 1,
                    .baseArrayLayer = 0,
                    .layerCount     = 6,
                },
            };
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &toGeneral);

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &mipSet, 0, nullptr);
            vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(float), &roughness);
            vkCmdDispatch(cmd, (mipSize + 7) / 8, (mipSize + 7) / 8, 6);

            VkImageMemoryBarrier toShaderRead = {
                .sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
                .oldLayout     = VK_IMAGE_LAYOUT_GENERAL,
                .newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                .image         = m_PrefilteredCubemap.image,
                .subresourceRange = {
                    .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel   = mip,
                    .levelCount     = 1,
                    .baseArrayLayer = 0,
                    .layerCount     = 6,
                },
            };
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &toShaderRead);

            EndOneTimeCommands(cmd);
        }

        // The per-mip storage views were only needed for the dispatches above;
        // sampling later goes through the single CUBE view spanning all mips.
        for (uint32_t mip = 0; mip < PREFILTER_MIP_COUNT; mip++) {
            vkDestroyImageView(m_Device.logicalDevice, m_PrefilterMipViews[mip], nullptr);
            m_PrefilterMipViews[mip] = VK_NULL_HANDLE;
        }

        OSIRIS_INFO("Specular prefiltered environment cubemap generated ({}x{} x6, {} mips)",
            PREFILTER_BASE_SIZE, PREFILTER_BASE_SIZE, PREFILTER_MIP_COUNT);
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
        // A minimized window reports a 0x0 surface extent, and vkCreateSwapchainKHR rejects that
        // outright (VUID-VkSwapchainCreateInfoKHR-imageExtent-01689). Block here, pumping events,
        // until the window is restored to a real size before touching any swapchain resources.
        int width = 0, height = 0;
        SDL_GetWindowSize(m_Desc.windowHandle, &width, &height);
        while (width == 0 || height == 0) {
            SDL_Event event;
            SDL_WaitEvent(&event);
            // SDL_WaitEvent removes the event from the queue, so a discarded SDL_QUIT here would
            // never reach Window::PollEvents' own SDL_PollEvent loop, meaning the app could never
            // close while sitting minimized. Push it back so that loop still sees it, and bail out
            // instead of trying to recreate a swapchain the app is about to shut down anyway.
            if (event.type == SDL_QUIT) {
                SDL_PushEvent(&event);
                return;
            }
            SDL_GetWindowSize(m_Desc.windowHandle, &width, &height);
        }

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

        DestroySceneColorImage();

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
        if (!CreateSceneColorImage()) {
            OSIRIS_ERROR("Failed to create scene color image on resize!");
            return;
        }
    }

void VulkanRHI::UpdateCascades(const glm::mat4& view, const glm::mat4& projection) {
    // Cascade split distances in view space
    float nearClip  = m_ShadowSettings.nearClip;
    float farClip   = m_ShadowSettings.farClip;
    float clipRange = farClip - nearClip;

    float cascadeSplitLambda = m_ShadowSettings.cascadeSplitLambda;

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
        glm::vec3 up = glm::abs(glm::dot(lightDir, glm::vec3(0,1,0))) < 0.8f
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

        // Texel snapping: frustumCenter drifts continuously as the camera moves, so without this
        // the shadow frustum's world-space footprint shifts by sub-texel amounts every frame,
        // sampling a slightly different sub-texel position each time and making shadow edges
        // visibly swim/jitter under camera motion. Find where the world origin lands in this
        // cascade's shadow space, snap that to the nearest whole texel, and fold the (tiny)
        // correction into the projection so the whole frustum only ever moves in texel-sized
        // steps. Standard technique for cascaded shadow maps, not specific to this engine.
        glm::vec4 shadowOrigin = (lightProj * lightView) * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        shadowOrigin *= static_cast<float>(SHADOW_MAP_SIZE) / 2.0f;
        glm::vec4 roundOffset = glm::round(shadowOrigin) - shadowOrigin;
        roundOffset *= 2.0f / static_cast<float>(SHADOW_MAP_SIZE);
        roundOffset.z = 0.0f;
        roundOffset.w = 0.0f;
        lightProj[3] += roundOffset;

        m_CascadeSplits[cascadeIndex]       = (nearClip + splitDist * clipRange) * -1.0f;
        m_LightViewMatrices[cascadeIndex] = lightView;
        m_LightProjMatrices[cascadeIndex] = lightProj;
        m_LightSpaceMatrices[cascadeIndex]  = lightProj * lightView;

        lastSplitDist = cascadeSplits[cascadeIndex];
    }
}

    void VulkanRHI::UpdateSpotLights(const std::vector<SpotLightRenderData>& lights) {
        SpotLightBufferFull buffer{};
        for (auto& matrix : buffer.shadowMatrices) matrix = glm::mat4(1.0f);

        uint32_t count = static_cast<uint32_t>(lights.size());
        if (count > MAX_SPOT_LIGHTS) count = MAX_SPOT_LIGHTS;

        for (uint32_t i = 0; i < count; i++) {
            const SpotLightRenderData& light = lights[i];
            const glm::vec3 direction = glm::normalize(light.direction);

            SpotLightGPU gpu{};
            gpu.position  = glm::vec4(light.position, light.range);
            gpu.direction = glm::vec4(direction, light.intensity);
            gpu.color     = glm::vec4(light.color, 0.0f);
            gpu.params    = glm::vec4(
                glm::cos(glm::radians(light.innerConeDegrees)),
                glm::cos(glm::radians(light.outerConeDegrees)),
                static_cast<float>(light.shadowIndex),
                0.0f);
            buffer.lights[i] = gpu;

            if (light.shadowIndex >= 0 && static_cast<uint32_t>(light.shadowIndex) < MAX_SPOT_SHADOW_CASTERS) {
                const uint32_t slot = static_cast<uint32_t>(light.shadowIndex);

                float fov    = glm::radians(light.outerConeDegrees * 2.0f);
                float aspect = 1.0f;
                float nearZ  = 0.1f;
                float farZ   = light.range;

                glm::vec3 up = glm::abs(glm::dot(direction, glm::vec3(0, 1, 0))) > 0.9f
                             ? glm::vec3(1, 0, 0)
                             : glm::vec3(0, 1, 0);

                glm::mat4 lightView = glm::lookAt(light.position, light.position + direction, up);
                glm::mat4 lightProj = glm::perspective(fov, aspect, nearZ, farZ);

                m_SpotShadowMatrices[slot] = lightProj * lightView;
                buffer.shadowMatrices[slot] = m_SpotShadowMatrices[slot];
            }
        }
        buffer.counts.x = static_cast<int32_t>(count);

        UploadDynamicBuffer(m_SpotLightUniformBuffer, &buffer, sizeof(SpotLightBufferFull));
    }

TextureHandle VulkanRHI::CreateSolidColorTexture(uint32_t r, uint32_t g, uint32_t b, uint32_t a,
                                                  TextureFormat format) {
        uint8_t pixels[4] = {
            static_cast<uint8_t>(r),
            static_cast<uint8_t>(g),
            static_cast<uint8_t>(b),
            static_cast<uint8_t>(a)
        };

        TextureDesc desc {
            .pixels = pixels,
            .dataSize = 4,
            .width = 1,
            .height = 1,
            .format = format,
        };

        return CreateTexture(desc);
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

        VkDescriptorImageInfo spotImageInfos[MAX_SPOT_SHADOW_CASTERS];
        for (uint32_t i = 0; i < MAX_SPOT_SHADOW_CASTERS; i++) {
            spotImageInfos[i] = {
                .sampler     = m_SpotShadowMaps[i].sampler,
                .imageView   = m_SpotShadowMaps[i].imageView,
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            };
        }
        VkWriteDescriptorSet spotWrite = {
            .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet          = m_DescriptorSet,
            .dstBinding      = 4,
            .dstArrayElement = 0,
            .descriptorCount = MAX_SPOT_SHADOW_CASTERS,
            .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo      = spotImageInfos,
        };
        vkUpdateDescriptorSets(m_Device.logicalDevice, 1, &spotWrite, 0, nullptr);

        OSIRIS_INFO("Shadow descriptors updated!");
    }
} // Osiris
