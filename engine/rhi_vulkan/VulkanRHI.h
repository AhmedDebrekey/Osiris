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

        void SetMeshData(const Mesh &mesh) override;

        BufferHandle CreateBuffer(const BufferDesc &) override;

        TextureHandle CreateTexture(const TextureDesc &) override;

        ShaderHandle CreateShader(const ShaderDesc &) override;

        void DestroyBuffer(BufferHandle) override;

        void DestroyTexture(TextureHandle) override;

        void DestroyShader(ShaderHandle) override;

        void BindPipeline(PipelineHandle pipeline) override;

        void Draw(uint32_t vertexCount) override;

        void DrawIndexed(uint32_t indexCount) override;

        void Dispatch(uint32_t x, uint32_t y, uint32_t z) override;

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
        bool CreatePipeline();
        bool CreateCommandBuffers();
        void RecordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex) const;

        void RecreateSwapChain();

        uint32_t AllocateBufferSlot(const VulkanBuffer& buffer);

        VkShaderModule LoadShader(const std::string& shaderPath);

        uint32_t m_ImageIndex = 0;

        VkInstance          m_Instance          = VK_NULL_HANDLE;
        VkSurfaceKHR        m_Surface           = VK_NULL_HANDLE;
        VkPipelineLayout    m_PipelineLayout    = VK_NULL_HANDLE;
        VkPipeline          m_GraphicsPipeline  = VK_NULL_HANDLE;
        VkCommandPool       m_CommandPool       = VK_NULL_HANDLE;

        std::array<VkSemaphore, MAX_FRAMES_IN_FLIGHT> m_ImageAvailableSemaphores;
        std::vector<VkSemaphore> m_RenderFinishedSemaphores;

        VulkanDevice       m_Device;
        VulkanSwapChain    m_SwapChain;

        std::array<VulkanFrameData, MAX_FRAMES_IN_FLIGHT> m_Frames;
        uint32_t m_CurrentFrame = 0;

        VmaAllocator m_Allocator = VK_NULL_HANDLE;
        std::vector<VulkanBuffer> m_Buffers;

        Mesh m_BoundMesh;
        VulkanContextDesc m_Desc;
    };
} // Osiris

#endif //OSIRIS_VULKANRHI_H