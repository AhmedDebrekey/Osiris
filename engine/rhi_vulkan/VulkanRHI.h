//
// Created by Debreky on 01/06/2026.
//

#ifndef OSIRIS_VULKANRHI_H
#define OSIRIS_VULKANRHI_H
#include <array>

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

        void RenderImGui() override;

        void Dispatch(uint32_t x, uint32_t y, uint32_t z) override;

        void UpdateCamera(const glm::mat4 &view, const glm::mat4 &projection, const glm::vec4& position) override;

        void BeginForwardPass() override;

        void BeginShadowPass(uint32_t cascadeIndex) override;

        void EndShadowPass(uint32_t cascadeIndex) override;

        void DrawShadowIndexed(uint32_t indexCount) override;

        void UpdateShadowDescriptors();

    private:
        bool SetupDebugMessenger();

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
        bool CreatePipeline();
        bool CreateDescriptorSetLayouts();
        bool CreateDescriptorPool();
        bool CreateDescriptorSet();
        bool CreateCommandBuffers();
        bool CreateShadowMap();

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

        std::array<VkSemaphore, MAX_FRAMES_IN_FLIGHT> m_ImageAvailableSemaphores;
        std::vector<VkSemaphore> m_RenderFinishedSemaphores;

        VulkanDevice       m_Device;
        VulkanSwapChain    m_SwapChain;
        VulkanImage        m_DepthImage;

        VulkanContextDesc m_Desc;

        RenderGraph m_RenderGraph;
        RGTexture m_ColorBufferRG;
        RGTexture m_DepthBufferRG;

        bool m_FrameStarted = false;

        std::array<VulkanFrameData, MAX_FRAMES_IN_FLIGHT> m_Frames;
        uint32_t m_CurrentFrame = 0;

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

        DirectionalLight m_DirectionalLight;
        glm::mat4 m_LightSpaceMatrices[SHADOW_CASCADE_COUNT];
        float m_CascadeSplits[SHADOW_CASCADE_COUNT];
        uint32_t m_CurrentCascadeIndex = 0;

        TextureHandle m_DefaultAlbedo;
        TextureHandle m_DefaultNormal;
        TextureHandle m_DefaultMetallic;
        TextureHandle m_DefaultRoughness;
        TextureHandle m_DefaultAO;
    };
} // Osiris

#endif //OSIRIS_VULKANRHI_H