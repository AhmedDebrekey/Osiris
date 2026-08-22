//
// Created by Debreky on 01/06/2026.
//

#ifndef OSIRIS_VULKANRHI_H
#define OSIRIS_VULKANRHI_H
#include <array>
#include <chrono>
#include <string>
#include <utility>
#include <vector>

#include "rhi/RHI.h"
#include "VulkanTypes.h"
#include "core/EngineConfig.h"
#include <vk_mem_alloc.h>

#include "renderer/RenderGraph.h"

#include "PipelineManager.h"
#include "renderer/Light.h"

namespace Osiris {
    constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

    class VulkanRHI : public IRHI{
    public:
        void Configure(const VulkanContextDesc& desc);

        bool Init() override;

        void Shutdown() override;

        void BeginFrame() override;

        void EndFrame() override;

        void Present() override;

        void BeginGPUTimestamp(const std::string& name) override;

        void EndGPUTimestamp(const std::string& name) override;

        std::vector<std::pair<std::string, float>> GetGPUTimings() const override { return m_GPUTimings; }

        void UploadBufferData(BufferHandle handle, const void* data, uint64_t size) override;

        void UploadDynamicBuffer(BufferHandle handle, const void *data, uint64_t size) override;

        void SetMeshData(const Mesh &mesh) override;

        void SetModelMatrix(const glm::mat4 &model) override;

        BufferHandle CreateBuffer(const BufferDesc &) override;

        TextureHandle CreateTexture(const TextureDesc &) override;

        ShaderHandle CreateShader(const ShaderDesc &) override;

        MaterialHandle CreateMaterial(const MaterialDesc &) override;

        void DestroyBuffer(BufferHandle) override;

        void DestroyTexture(TextureHandle) override;

        void DestroyShader(ShaderHandle) override;

        void BindMaterial(MaterialHandle handle) override;

        void BindPipeline(PipelineHandle pipeline) override;

        void Draw(uint32_t vertexCount) override;

        void DrawIndexed(uint32_t indexCount) override;

        void InitImGui() override;

        void ShutdownImGui() override;

        void BeginImGuiFrame() override;

        void RenderImGui(bool separatePass) override;

        void Dispatch(uint32_t x, uint32_t y, uint32_t z) override;

        DirectionalLight& GetDirectionalLight() override { return m_DirectionalLight; }
        glm::mat4 GetLightViewMatrix(uint32_t cascade) const override { return m_LightViewMatrices[cascade]; }
        glm::mat4 GetLightProjMatrix(uint32_t cascade) const override { return m_LightProjMatrices[cascade]; }
        ShadowSettings& GetShadowSettings() override {return m_ShadowSettings;}

        void UpdateSpotLights(const std::vector<SpotLightRenderData>& lights) override;

        bool LoadEnvironmentMap(const float* pixels, uint32_t width, uint32_t height) override;
        float& GetEnvironmentExposure() override { return m_EnvironmentExposure; }

        void UpdateCamera(const glm::mat4 &view, const glm::mat4 &projection, const glm::vec4& position, const glm::vec3& front) override;
        void SetCameraBuffer(const glm::mat4& view, const glm::mat4& projection, const glm::vec4& position) override;

        void BeginForwardPass() override;
        void BeginViewportForwardPass() override;
        void ResizeViewport(uint32_t width, uint32_t height) override;
        uint64_t GetViewportTextureID() const override { return m_ViewportTextureID; }
        uint64_t GetShadowCascadeTextureID(uint32_t cascade) const override {
            return cascade < m_ShadowCascadeTextureIDs.size() ? m_ShadowCascadeTextureIDs[cascade] : 0;
        }
        uint64_t GetSpotShadowTextureID(uint32_t index) const override {
            return index < m_SpotShadowTextureIDs.size() ? m_SpotShadowTextureIDs[index] : 0;
        }
        glm::uvec2 GetRenderExtent(bool viewport) const override;

        void BeginShadowPass(uint32_t cascadeIndex) override;

        void EndShadowPass(uint32_t cascadeIndex) override;

        void DrawShadowIndexed(uint32_t indexCount) override;

        void BeginSpotShadowPass(uint32_t index) override;

        void EndSpotShadowPass(uint32_t index)   override;

        void UpdateShadowDescriptors();

        glm::mat4 GetLightSpaceMatrix(uint32_t cascadeIndex) const override {
            return m_LightSpaceMatrices[cascadeIndex];
        }

    private:
        struct GPUTimestampScope {
            std::string name;
            uint32_t startQuery = 0;
            uint32_t endQuery = UINT32_MAX;
        };

        struct GPUTimestampFrame {
            VkQueryPool queryPool = VK_NULL_HANDLE;
            uint32_t queryCount = 0;
            std::vector<GPUTimestampScope> scopes;
        };

        bool SetupDebugMessenger();
        static void ApplyEditorTheme();

        VkDebugUtilsMessengerEXT m_DebugMessenger = VK_NULL_HANDLE;

        static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                            VkDebugUtilsMessageTypeFlagsEXT type,
                                                            const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
                                                            void* userData);


        bool SelectPhysicalDevice();
        bool CreateLogicalDevice();
        bool CreateSurface();
        bool CreateSwapChain();
        bool CreateSwapChainImages();
        bool CreateDepthResources();
        bool CreateViewportResources(uint32_t width, uint32_t height);
        void DestroyViewportResources();
        void BeginScenePass(VulkanImage& colorImage, VulkanImage& depthImage, VkExtent2D extent,
                            ResourceState colorInitialState);
        bool CreatePipeline();
        bool CreateDescriptorSetLayouts();
        bool CreateDescriptorPool();
        bool CreateDescriptorSet();
        bool CreateCommandBuffers();
        bool CreateShadowMap();

        bool GenerateBRDFLUT();
        bool CreateDefaultEnvironmentCubemap();
        bool ConvolveIrradiance();
        bool PrefilterEnvironment();

        void RecreateSwapChain();

        void UpdateCascades(const glm::mat4& view, const glm::mat4& projection);

        TextureHandle CreateSolidColorTexture(uint32_t r, uint32_t g, uint32_t b, uint32_t a);

        void WriteToDescriptorSet(TextureHandle textureHandle, uint32_t dstBinding, VkDescriptorSet &descriptorSet);

        VkShaderModule LoadShader(const std::string& shaderPath);

        void TransitionImageLayout(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout);
        VkCommandBuffer BeginOneTimeCommands();
        void EndOneTimeCommands(VkCommandBuffer cmd);

    private:
        uint32_t m_ImageIndex = 0;

        VkInstance              m_Instance                  = VK_NULL_HANDLE;
        VkSurfaceKHR            m_Surface                   = VK_NULL_HANDLE;
        VkCommandPool           m_CommandPool               = VK_NULL_HANDLE;
        VkDescriptorSetLayout   m_FrameDescriptorLayout     = VK_NULL_HANDLE;
        VkDescriptorSetLayout   m_MaterialDescriptorLayout  = VK_NULL_HANDLE;
        VkDescriptorPool        m_DescriptorPool            = VK_NULL_HANDLE;
        VkDescriptorSet         m_DescriptorSet             = VK_NULL_HANDLE;
        VkDescriptorPool        m_ImGuiDescriptorPool       = VK_NULL_HANDLE;

        std::unique_ptr<PipelineManager> m_PipelineManager;
        VkPipeline          m_ForwardPipeline       = VK_NULL_HANDLE;
        VkPipelineLayout    m_ForwardPipelineLayout = VK_NULL_HANDLE;
        VkPipeline          m_ShadowPipeline        = VK_NULL_HANDLE;
        VkPipelineLayout    m_ShadowPipelineLayout  = VK_NULL_HANDLE;
        VkPipeline          m_SkyboxPipeline        = VK_NULL_HANDLE;
        VkPipelineLayout    m_SkyboxPipelineLayout  = VK_NULL_HANDLE;

        std::array<VkSemaphore, MAX_FRAMES_IN_FLIGHT> m_ImageAvailableSemaphores;
        std::vector<VkSemaphore> m_RenderFinishedSemaphores;

        VulkanDevice       m_Device;
        VulkanSwapChain    m_SwapChain;
        VulkanImage        m_DepthImage;
        VulkanImage        m_ViewportColorImage;
        VulkanImage        m_ViewportDepthImage;
        VkExtent2D         m_ViewportExtent = {};
        VkExtent2D         m_PendingViewportExtent = {};
        std::chrono::steady_clock::time_point m_ViewportResizeRequestedAt = {};
        uint64_t           m_ViewportTextureID = 0;
        bool               m_ViewportImageInitialized = false;
        bool               m_RenderingViewport = false;

        VulkanContextDesc m_Desc;

        RenderGraph m_RenderGraph;
        RGTexture m_ColorBufferRG;
        RGTexture m_DepthBufferRG;

        bool m_FrameStarted = false;

        std::array<VulkanFrameData, MAX_FRAMES_IN_FLIGHT> m_Frames;
        uint32_t m_CurrentFrame = 0;

        static constexpr uint32_t GPU_TIMESTAMP_QUERY_COUNT = 64;
        std::array<GPUTimestampFrame, MAX_FRAMES_IN_FLIGHT> m_GPUTimestampFrames;
        std::vector<std::pair<std::string, float>> m_GPUTimings;
        float m_TimestampPeriod = 0.0f;

        VmaAllocator m_Allocator = VK_NULL_HANDLE;
        std::vector<VulkanBuffer> m_Buffers;
        std::vector<VulkanImage> m_Textures;
        std::vector<VulkanMaterial> m_Materials;

        BufferHandle m_CameraUniformBuffer;

        Mesh m_BoundMesh;
        TextureHandle m_BoundTexture;
        glm::mat4 m_ModelMatrix = glm::mat4(1.0f);

        static constexpr uint32_t SHADOW_CASCADE_COUNT = 3;
        static constexpr uint32_t SHADOW_MAP_SIZE      = 2048;
        VulkanImage m_ShadowMaps[SHADOW_CASCADE_COUNT];
        std::array<uint64_t, SHADOW_CASCADE_COUNT> m_ShadowCascadeTextureIDs = {};

        static constexpr uint32_t SPOT_SHADOW_MAP_SIZE = 1024;
        VulkanImage m_SpotShadowMaps[MAX_SPOT_SHADOW_CASTERS];
        std::array<uint64_t, MAX_SPOT_SHADOW_CASTERS> m_SpotShadowTextureIDs = {};
        glm::mat4   m_SpotShadowMatrices[MAX_SPOT_SHADOW_CASTERS];
        BufferHandle m_SpotLightUniformBuffer;

        ShadowSettings m_ShadowSettings;

        DirectionalLight m_DirectionalLight;
        glm::mat4 m_LightViewMatrices[SHADOW_CASCADE_COUNT];
        glm::mat4 m_LightProjMatrices[SHADOW_CASCADE_COUNT];
        glm::mat4 m_LightSpaceMatrices[SHADOW_CASCADE_COUNT];
        float m_CascadeSplits[SHADOW_CASCADE_COUNT];
        uint32_t m_CurrentCascadeIndex = 0;

        glm::mat4 m_ActiveLightSpaceMatrix;

        TextureHandle m_DefaultAlbedo;
        TextureHandle m_DefaultNormal;
        TextureHandle m_DefaultMetallic;
        TextureHandle m_DefaultRoughness;
        TextureHandle m_DefaultAO;

        // IBL (Phase 6D). Built once at startup, entirely internal — not routed
        // through the generic CreateTexture path (same reasoning as shadow maps:
        // needs storage-image usage, custom formats/views CreateTexture doesn't
        // support).
        static constexpr uint32_t BRDF_LUT_SIZE = 512;
        VkDescriptorSetLayout m_BRDFLutComputeLayout = VK_NULL_HANDLE;
        VulkanImage m_BRDFLut;

        static constexpr uint32_t ENV_CUBEMAP_SIZE = 512;
        VkDescriptorSetLayout m_EquirectToCubemapLayout = VK_NULL_HANDLE;
        VulkanCubemapImage m_EnvironmentCubemap;
        bool m_EnvironmentLoaded = false;

        // Placeholder for frame set bindings 6-8 until LoadEnvironmentMap
        // succeeds — keeps those statically-referenced IBL bindings valid
        // even if no environment is ever loaded (see triangle.frag).
        VulkanCubemapImage m_DefaultEnvironmentCubemap;
        float m_EnvironmentExposure = 0.35f; // sweet spot found for EveningRoad.hdr; adjust per-environment

        static constexpr uint32_t IRRADIANCE_CUBEMAP_SIZE = 32;
        VkDescriptorSetLayout m_IrradianceConvolveLayout = VK_NULL_HANDLE;
        VulkanCubemapImage m_IrradianceCubemap;

        static constexpr uint32_t PREFILTER_BASE_SIZE = 128;
        static constexpr uint32_t PREFILTER_MIP_COUNT  = 5;
        VkDescriptorSetLayout m_PrefilterConvolveLayout = VK_NULL_HANDLE;
        VulkanCubemapImage m_PrefilteredCubemap;
        VkImageView m_PrefilterMipViews[PREFILTER_MIP_COUNT] = {};
    };
} // Osiris

#endif //OSIRIS_VULKANRHI_H
