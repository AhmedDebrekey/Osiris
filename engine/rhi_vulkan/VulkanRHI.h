//
// Created by Debreky on 01/06/2026.
//

#ifndef OSIRIS_VULKANRHI_H
#define OSIRIS_VULKANRHI_H
#include <array>

#include "rhi/RHI.h"
#include "VulkanTypes.h"

namespace Osiris {
    constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

    class VulkanRHI : public IRHI{
    public:
        bool Init() override;

        void Shutdown() override;

        void BeginFrame() override;

        void EndFrame() override;

        void Present() override;

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
        VkInstance      m_Instance      = VK_NULL_HANDLE;
        VkSurfaceKHR    m_Surface       = VK_NULL_HANDLE;
        VkCommandPool   m_CommandPool   = VK_NULL_HANDLE;

        VulkanDevice       m_Device;
        VulkanSwapChain    m_SwapChain;

        std::array<VulkanFrameData, MAX_FRAMES_IN_FLIGHT> m_Frames;
        uint32_t m_CurrentFrame = 0;
    };
} // Osiris

#endif //OSIRIS_VULKANRHI_H