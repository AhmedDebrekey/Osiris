//
// Created by Debreky on 07/08/2026.
//

#ifndef OSIRIS_PIPELINEMANAGER_H
#define OSIRIS_PIPELINEMANAGER_H

#pragma once

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Osiris
{
    struct PipelineDesc
    {
        // SPIR-V shader files.
        std::filesystem::path vertexShader;
        std::filesystem::path fragmentShader;

        // Dynamic-rendering attachment configuration.
        bool colorAttachment = true;
        VkFormat colorFormat = VK_FORMAT_B8G8R8A8_UNORM;

        bool depthAttachment = true;
        VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;

        // Depth/stencil state.
        bool depthTest = true;
        bool depthWrite = true;
        VkCompareOp depthCompareOp = VK_COMPARE_OP_LESS;

        bool depthBias = false;
        float depthBiasConstant = 0.0f;
        float depthBiasClamp = 0.0f;
        float depthBiasSlope = 0.0f;

        // Rasterization state.
        VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT;
        VkFrontFace frontFace = VK_FRONT_FACE_CLOCKWISE;
        VkPolygonMode polygonMode = VK_POLYGON_MODE_FILL;

        // Input assembly.
        VkPrimitiveTopology topology =
            VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        // Multisampling.
        VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;

        // False for bufferless fullscreen-triangle passes (skybox, post-
        // processing) whose vertex shader generates positions from
        // gl_VertexIndex instead of reading the Vertex struct.
        bool vertexInput = true;

        // Pipeline layout.
        uint32_t setLayoutCount = 0;
        const VkDescriptorSetLayout* pSetLayouts = nullptr;

        uint32_t pushConstantSize = 0;
        VkShaderStageFlags pushConstantStages =
            VK_SHADER_STAGE_VERTEX_BIT |
            VK_SHADER_STAGE_FRAGMENT_BIT;

        [[nodiscard]] bool operator==(const PipelineDesc& rhs) const noexcept;
    };

    struct PipelineDescHash
    {
        [[nodiscard]] std::size_t operator()(
            const PipelineDesc& desc) const noexcept;
    };

    struct ComputePipelineDesc
    {
        // SPIR-V shader file.
        std::filesystem::path computeShader;

        // Pipeline layout.
        uint32_t setLayoutCount = 0;
        const VkDescriptorSetLayout* pSetLayouts = nullptr;

        uint32_t pushConstantSize = 0;
        VkShaderStageFlags pushConstantStages = VK_SHADER_STAGE_COMPUTE_BIT;

        [[nodiscard]] bool operator==(const ComputePipelineDesc& rhs) const noexcept;
    };

    struct ComputePipelineDescHash
    {
        [[nodiscard]] std::size_t operator()(
            const ComputePipelineDesc& desc) const noexcept;
    };

    class PipelineManager final
    {
    public:
        explicit PipelineManager(
            VkDevice device,
            VkPipelineCache pipelineCache = VK_NULL_HANDLE,
            const VkAllocationCallbacks* allocator = nullptr);

        ~PipelineManager();

        PipelineManager(const PipelineManager&) = delete;
        PipelineManager& operator=(const PipelineManager&) = delete;
        PipelineManager(PipelineManager&&) = delete;
        PipelineManager& operator=(PipelineManager&&) = delete;

        /// Returns an existing compatible pipeline or creates a new one.
        [[nodiscard]] VkPipeline GetOrCreate(const PipelineDesc& desc);

        /// Returns the layout associated with a cached pipeline.
        ///
        /// Required for vkCmdBindDescriptorSets and vkCmdPushConstants.
        [[nodiscard]] VkPipelineLayout GetLayout(
            VkPipeline pipeline) const;

        /// Convenience overload that retrieves or creates the pipeline first.
        [[nodiscard]] VkPipelineLayout GetOrCreateLayout(
            const PipelineDesc& desc);

        /// Returns an existing compatible compute pipeline or creates a new one.
        [[nodiscard]] VkPipeline GetOrCreateCompute(
            const ComputePipelineDesc& desc);

        /// Destroys every cached pipeline and pipeline layout.
        ///
        /// The caller must ensure no submitted command buffer is still using
        /// these objects before calling Shutdown().
        void Shutdown();

        [[nodiscard]] std::size_t GetPipelineCount() const noexcept;

    private:
        struct PipelineKey
        {
            std::string vertexShader;
            std::string fragmentShader;

            bool colorAttachment = true;
            VkFormat colorFormat = VK_FORMAT_UNDEFINED;

            bool depthAttachment = true;
            VkFormat depthFormat = VK_FORMAT_UNDEFINED;

            bool depthTest = true;
            bool depthWrite = true;
            VkCompareOp depthCompareOp = VK_COMPARE_OP_LESS;

            bool depthBias = false;
            float depthBiasConstant = 0.0f;
            float depthBiasClamp = 0.0f;
            float depthBiasSlope = 0.0f;

            VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT;
            VkFrontFace frontFace = VK_FRONT_FACE_CLOCKWISE;
            VkPolygonMode polygonMode = VK_POLYGON_MODE_FILL;
            VkPrimitiveTopology topology =
                VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
            bool vertexInput = true;

            std::vector<VkDescriptorSetLayout> setLayouts;

            uint32_t pushConstantSize = 0;
            VkShaderStageFlags pushConstantStages = 0;

            [[nodiscard]] bool operator==(
                const PipelineKey& rhs) const noexcept = default;
        };

        struct PipelineKeyHash
        {
            [[nodiscard]] std::size_t operator()(
                const PipelineKey& key) const noexcept;
        };

        struct PipelineEntry
        {
            VkPipeline pipeline = VK_NULL_HANDLE;
            VkPipelineLayout layout = VK_NULL_HANDLE;
        };

        struct ComputePipelineKey
        {
            std::string computeShader;

            std::vector<VkDescriptorSetLayout> setLayouts;

            uint32_t pushConstantSize = 0;
            VkShaderStageFlags pushConstantStages = 0;

            [[nodiscard]] bool operator==(
                const ComputePipelineKey& rhs) const noexcept = default;
        };

        struct ComputePipelineKeyHash
        {
            [[nodiscard]] std::size_t operator()(
                const ComputePipelineKey& key) const noexcept;
        };

        [[nodiscard]] static PipelineKey MakeKey(
            const PipelineDesc& desc);

        [[nodiscard]] static ComputePipelineKey MakeComputeKey(
            const ComputePipelineDesc& desc);

        static void ValidateDesc(const PipelineDesc& desc);

        static void ValidateComputeDesc(const ComputePipelineDesc& desc);

        [[nodiscard]] PipelineEntry CreatePipeline(
            const PipelineKey& key) const;

        [[nodiscard]] PipelineEntry CreateComputePipelineEntry(
            const ComputePipelineKey& key) const;

        [[nodiscard]] VkPipelineLayout CreatePipelineLayout(
            const PipelineKey& key) const;

        [[nodiscard]] VkPipelineLayout CreateComputePipelineLayout(
            const ComputePipelineKey& key) const;

        [[nodiscard]] VkShaderModule LoadShaderModule(
            std::string_view path) const;

        VkDevice m_Device = VK_NULL_HANDLE;
        VkPipelineCache m_PipelineCache = VK_NULL_HANDLE;
        const VkAllocationCallbacks* m_Allocator = nullptr;

        std::unordered_map<PipelineKey, PipelineEntry, PipelineKeyHash>
            m_Pipelines;

        std::unordered_map<ComputePipelineKey, PipelineEntry, ComputePipelineKeyHash>
            m_ComputePipelines;

        std::unordered_map<VkPipeline, VkPipelineLayout>
            m_LayoutsByPipeline;

        mutable std::mutex m_Mutex;
    };
}

#endif //OSIRIS_PIPELINEMANAGER_H