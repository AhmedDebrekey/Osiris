//
// Created by ahtal on 07/08/2026.
//

#include "PipelineManager.h"

#include "../renderer/MeshType.h"

#include <algorithm>
#include <array>
#include <bit>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <type_traits>
#include <utility>

#include "core/Log.h"

namespace Osiris
{
    namespace
    {
        template<typename T>
        void HashCombine(std::size_t& seed, const T& value) noexcept
        {
            const std::size_t hash = std::hash<T>{}(value);

            // 64-bit-friendly variant of boost-style hash combination.
            seed ^= hash +
                    static_cast<std::size_t>(0x9e3779b97f4a7c15ULL) +
                    (seed << 6U) +
                    (seed >> 2U);
        }

        void HashFloat(std::size_t& seed, float value) noexcept
        {
            HashCombine(seed, std::bit_cast<uint32_t>(value));
        }

        [[nodiscard]] std::string NormalizePath(
            const std::filesystem::path& path)
        {
            if (path.empty())
            {
                return {};
            }

            std::error_code error;

            // weakly_canonical() helps ensure equivalent paths share a cache
            // entry, but can fail if parts of the path do not exist.
            std::filesystem::path normalized =
                std::filesystem::weakly_canonical(path, error);

            if (error)
            {
                normalized = path.lexically_normal();
            }

            return normalized.generic_string();
        }

        [[nodiscard]] std::vector<char> ReadBinaryFile(
            std::string_view filename)
        {
            std::ifstream file(
                std::string(filename),
                std::ios::ate | std::ios::binary);

            if (!file.is_open())
            {
                OSIRIS_ERROR("PipelineManager: failed to open shader file: ");
                return {};
            }

            const std::streampos endPosition = file.tellg();

            if (endPosition <= 0)
            {
                OSIRIS_ERROR("PipelineManager: shader file is empty:  ");

            }

            const auto fileSize =
                static_cast<std::uintmax_t>(endPosition);

            if (fileSize >
                static_cast<std::uintmax_t>(
                    std::numeric_limits<std::size_t>::max()))
            {
                OSIRIS_ERROR("PipelineManager:Shader file is too large: ");
            }

            std::vector<char> bytes(
                static_cast<std::size_t>(fileSize));

            file.seekg(0, std::ios::beg);

            if (!file.read(
                    bytes.data(),
                    static_cast<std::streamsize>(bytes.size())))
            {
                OSIRIS_ERROR("PipelineManager: failed to read shader file: ");
            }

            return bytes;
        }

        [[nodiscard]] bool DescriptorLayoutsEqual(
            const PipelineDesc& lhs,
            const PipelineDesc& rhs) noexcept
        {
            if (lhs.setLayoutCount != rhs.setLayoutCount)
            {
                return false;
            }

            if (lhs.setLayoutCount == 0)
            {
                return true;
            }

            if (lhs.pSetLayouts == nullptr ||
                rhs.pSetLayouts == nullptr)
            {
                return lhs.pSetLayouts == rhs.pSetLayouts;
            }

            for (uint32_t i = 0; i < lhs.setLayoutCount; ++i)
            {
                if (lhs.pSetLayouts[i] != rhs.pSetLayouts[i])
                {
                    return false;
                }
            }

            return true;
        }
    }

    bool PipelineDesc::operator==(
        const PipelineDesc& rhs) const noexcept
    {
        return vertexShader == rhs.vertexShader &&
               fragmentShader == rhs.fragmentShader &&
               colorAttachment == rhs.colorAttachment &&
               colorFormat == rhs.colorFormat &&
               depthAttachment == rhs.depthAttachment &&
               depthFormat == rhs.depthFormat &&
               depthTest == rhs.depthTest &&
               depthWrite == rhs.depthWrite &&
               depthCompareOp == rhs.depthCompareOp &&
               depthBias == rhs.depthBias &&
               depthBiasConstant == rhs.depthBiasConstant &&
               depthBiasClamp == rhs.depthBiasClamp &&
               depthBiasSlope == rhs.depthBiasSlope &&
               alphaBlend == rhs.alphaBlend &&
               cullMode == rhs.cullMode &&
               frontFace == rhs.frontFace &&
               polygonMode == rhs.polygonMode &&
               topology == rhs.topology &&
               samples == rhs.samples &&
               vertexInput == rhs.vertexInput &&
               pushConstantSize == rhs.pushConstantSize &&
               pushConstantStages == rhs.pushConstantStages &&
               DescriptorLayoutsEqual(*this, rhs);
    }

    std::size_t PipelineDescHash::operator()(
        const PipelineDesc& desc) const noexcept
    {
        std::size_t seed = 0;

        HashCombine(seed, desc.vertexShader.generic_string());
        HashCombine(seed, desc.fragmentShader.generic_string());

        HashCombine(seed, desc.colorAttachment);
        HashCombine(seed, static_cast<int32_t>(desc.colorFormat));

        HashCombine(seed, desc.depthAttachment);
        HashCombine(seed, static_cast<int32_t>(desc.depthFormat));

        HashCombine(seed, desc.depthTest);
        HashCombine(seed, desc.depthWrite);
        HashCombine(seed, static_cast<int32_t>(desc.depthCompareOp));

        HashCombine(seed, desc.depthBias);
        HashFloat(seed, desc.depthBiasConstant);
        HashFloat(seed, desc.depthBiasClamp);
        HashFloat(seed, desc.depthBiasSlope);
        HashCombine(seed, desc.alphaBlend);

        HashCombine(seed, desc.cullMode);
        HashCombine(seed, static_cast<int32_t>(desc.frontFace));
        HashCombine(seed, static_cast<int32_t>(desc.polygonMode));
        HashCombine(seed, static_cast<int32_t>(desc.topology));
        HashCombine(seed, static_cast<int32_t>(desc.samples));
        HashCombine(seed, desc.vertexInput);

        HashCombine(seed, desc.setLayoutCount);

        if (desc.pSetLayouts != nullptr)
        {
            for (uint32_t i = 0; i < desc.setLayoutCount; ++i)
            {
                HashCombine(seed, desc.pSetLayouts[i]);
            }
        }

        HashCombine(seed, desc.pushConstantSize);
        HashCombine(seed, desc.pushConstantStages);

        return seed;
    }

    bool ComputePipelineDesc::operator==(
        const ComputePipelineDesc& rhs) const noexcept
    {
        return computeShader == rhs.computeShader &&
               pushConstantSize == rhs.pushConstantSize &&
               pushConstantStages == rhs.pushConstantStages &&
               setLayoutCount == rhs.setLayoutCount &&
               (setLayoutCount == 0 ||
                std::equal(
                    pSetLayouts, pSetLayouts + setLayoutCount,
                    rhs.pSetLayouts));
    }

    std::size_t ComputePipelineDescHash::operator()(
        const ComputePipelineDesc& desc) const noexcept
    {
        std::size_t seed = 0;

        HashCombine(seed, desc.computeShader.generic_string());
        HashCombine(seed, desc.pushConstantSize);
        HashCombine(seed, desc.pushConstantStages);
        HashCombine(seed, desc.setLayoutCount);

        if (desc.pSetLayouts != nullptr)
        {
            for (uint32_t i = 0; i < desc.setLayoutCount; ++i)
            {
                HashCombine(seed, desc.pSetLayouts[i]);
            }
        }

        return seed;
    }

    PipelineManager::PipelineManager(
        VkDevice device,
        VkPipelineCache pipelineCache,
        const VkAllocationCallbacks* allocator)
        : m_Device(device),
          m_PipelineCache(pipelineCache),
          m_Allocator(allocator)
    {
        if (m_Device == VK_NULL_HANDLE)
        {
            OSIRIS_ERROR("PipelineManager:VkDevice must not be NULL: ");

        }
    }

    PipelineManager::~PipelineManager()
    {
        Shutdown();
    }

    VkPipeline PipelineManager::GetOrCreate(
        const PipelineDesc& desc)
    {
        ValidateDesc(desc);

        PipelineKey key = MakeKey(desc);

        std::scoped_lock lock(m_Mutex);

        if (const auto existing = m_Pipelines.find(key);
            existing != m_Pipelines.end())
        {
            return existing->second.pipeline;
        }

        PipelineEntry entry = CreatePipeline(key);
        const VkPipeline pipeline = entry.pipeline;

        try
        {
            const auto [iterator, inserted] =
                m_Pipelines.emplace(std::move(key), entry);

            if (!inserted)
            {
                // Defensive path for future changes. With the mutex held,
                // another thread cannot normally insert the same key here.
                vkDestroyPipeline(
                    m_Device,
                    entry.pipeline,
                    m_Allocator);

                vkDestroyPipelineLayout(
                    m_Device,
                    entry.layout,
                    m_Allocator);

                return iterator->second.pipeline;
            }

            m_LayoutsByPipeline.emplace(
                entry.pipeline,
                entry.layout);
        }
        catch (...)
        {
            vkDestroyPipeline(
                m_Device,
                entry.pipeline,
                m_Allocator);

            vkDestroyPipelineLayout(
                m_Device,
                entry.layout,
                m_Allocator);
            OSIRIS_ERROR("PipelineManager: Destroying pipeline and Pipeline layout ");

        }

        return pipeline;
    }

    VkPipelineLayout PipelineManager::GetLayout(
        VkPipeline pipeline) const
    {
        if (pipeline == VK_NULL_HANDLE)
        {
            OSIRIS_ERROR("PipelineManager: GetLayout: pipeline is null ");

        }

        std::scoped_lock lock(m_Mutex);

        const auto iterator = m_LayoutsByPipeline.find(pipeline);

        if (iterator == m_LayoutsByPipeline.end())
        {
            OSIRIS_ERROR("PipelineManager::GetLayout: pipeline is not owned "
                "by this manager");

        }

        return iterator->second;
    }

    VkPipelineLayout PipelineManager::GetOrCreateLayout(
        const PipelineDesc& desc)
    {
        const VkPipeline pipeline = GetOrCreate(desc);
        return GetLayout(pipeline);
    }

    VkPipeline PipelineManager::GetOrCreateCompute(
        const ComputePipelineDesc& desc)
    {
        ValidateComputeDesc(desc);

        ComputePipelineKey key = MakeComputeKey(desc);

        std::scoped_lock lock(m_Mutex);

        if (const auto existing = m_ComputePipelines.find(key);
            existing != m_ComputePipelines.end())
        {
            return existing->second.pipeline;
        }

        PipelineEntry entry = CreateComputePipelineEntry(key);
        const VkPipeline pipeline = entry.pipeline;

        try
        {
            const auto [iterator, inserted] =
                m_ComputePipelines.emplace(std::move(key), entry);

            if (!inserted)
            {
                vkDestroyPipeline(
                    m_Device,
                    entry.pipeline,
                    m_Allocator);

                vkDestroyPipelineLayout(
                    m_Device,
                    entry.layout,
                    m_Allocator);

                return iterator->second.pipeline;
            }

            m_LayoutsByPipeline.emplace(
                entry.pipeline,
                entry.layout);
        }
        catch (...)
        {
            vkDestroyPipeline(
                m_Device,
                entry.pipeline,
                m_Allocator);

            vkDestroyPipelineLayout(
                m_Device,
                entry.layout,
                m_Allocator);
            OSIRIS_ERROR("PipelineManager: Destroying compute pipeline and pipeline layout");
        }

        return pipeline;
    }

    void PipelineManager::Shutdown()
    {
        std::scoped_lock lock(m_Mutex);

        if (m_Device == VK_NULL_HANDLE)
        {
            return;
        }

        for (const auto& [key, entry] : m_Pipelines)
        {
            static_cast<void>(key);

            if (entry.pipeline != VK_NULL_HANDLE)
            {
                vkDestroyPipeline(
                    m_Device,
                    entry.pipeline,
                    m_Allocator);
            }

            if (entry.layout != VK_NULL_HANDLE)
            {
                vkDestroyPipelineLayout(
                    m_Device,
                    entry.layout,
                    m_Allocator);
            }
        }

        for (const auto& [key, entry] : m_ComputePipelines)
        {
            static_cast<void>(key);

            if (entry.pipeline != VK_NULL_HANDLE)
            {
                vkDestroyPipeline(
                    m_Device,
                    entry.pipeline,
                    m_Allocator);
            }

            if (entry.layout != VK_NULL_HANDLE)
            {
                vkDestroyPipelineLayout(
                    m_Device,
                    entry.layout,
                    m_Allocator);
            }
        }

        m_LayoutsByPipeline.clear();
        m_Pipelines.clear();
        m_ComputePipelines.clear();
    }

    std::size_t PipelineManager::GetPipelineCount() const noexcept
    {
        std::scoped_lock lock(m_Mutex);
        return m_Pipelines.size();
    }

    std::size_t PipelineManager::PipelineKeyHash::operator()(
        const PipelineKey& key) const noexcept
    {
        std::size_t seed = 0;

        HashCombine(seed, key.vertexShader);
        HashCombine(seed, key.fragmentShader);

        HashCombine(seed, key.colorAttachment);
        HashCombine(seed, static_cast<int32_t>(key.colorFormat));

        HashCombine(seed, key.depthAttachment);
        HashCombine(seed, static_cast<int32_t>(key.depthFormat));

        HashCombine(seed, key.depthTest);
        HashCombine(seed, key.depthWrite);
        HashCombine(seed, static_cast<int32_t>(key.depthCompareOp));

        HashCombine(seed, key.depthBias);
        HashFloat(seed, key.depthBiasConstant);
        HashFloat(seed, key.depthBiasClamp);
        HashFloat(seed, key.depthBiasSlope);
        HashCombine(seed, key.alphaBlend);

        HashCombine(seed, key.cullMode);
        HashCombine(seed, static_cast<int32_t>(key.frontFace));
        HashCombine(seed, static_cast<int32_t>(key.polygonMode));
        HashCombine(seed, static_cast<int32_t>(key.topology));
        HashCombine(seed, static_cast<int32_t>(key.samples));
        HashCombine(seed, key.vertexInput);

        HashCombine(seed, key.setLayouts.size());

        for (VkDescriptorSetLayout layout : key.setLayouts)
        {
            HashCombine(seed, layout);
        }

        HashCombine(seed, key.pushConstantSize);
        HashCombine(seed, key.pushConstantStages);

        return seed;
    }

    PipelineManager::PipelineKey PipelineManager::MakeKey(
        const PipelineDesc& desc)
    {
        PipelineKey key{};

        key.vertexShader = NormalizePath(desc.vertexShader);
        key.fragmentShader = NormalizePath(desc.fragmentShader);

        key.colorAttachment = desc.colorAttachment;
        key.colorFormat = desc.colorAttachment
            ? desc.colorFormat
            : VK_FORMAT_UNDEFINED;

        key.depthAttachment = desc.depthAttachment;
        key.depthFormat = desc.depthAttachment
            ? desc.depthFormat
            : VK_FORMAT_UNDEFINED;

        key.depthTest = desc.depthTest;
        key.depthWrite = desc.depthWrite;
        key.depthCompareOp = desc.depthCompareOp;

        key.depthBias = desc.depthBias;
        key.depthBiasConstant = desc.depthBiasConstant;
        key.depthBiasClamp = desc.depthBiasClamp;
        key.depthBiasSlope = desc.depthBiasSlope;
        key.alphaBlend = desc.alphaBlend;

        key.cullMode = desc.cullMode;
        key.frontFace = desc.frontFace;
        key.polygonMode = desc.polygonMode;
        key.topology = desc.topology;
        key.samples = desc.samples;
        key.vertexInput = desc.vertexInput;

        if (desc.setLayoutCount > 0)
        {
            key.setLayouts.assign(
                desc.pSetLayouts,
                desc.pSetLayouts + desc.setLayoutCount);
        }

        key.pushConstantSize = desc.pushConstantSize;

        if (desc.pushConstantSize > 0)
        {
            key.pushConstantStages = desc.pushConstantStages;

            // A fragment stage cannot access push constants if the pipeline
            // has no fragment shader.
            if (key.fragmentShader.empty())
            {
                key.pushConstantStages &=
                    ~VK_SHADER_STAGE_FRAGMENT_BIT;
            }
        }
        else
        {
            key.pushConstantStages = 0;
        }

        return key;
    }

    std::size_t PipelineManager::ComputePipelineKeyHash::operator()(
        const ComputePipelineKey& key) const noexcept
    {
        std::size_t seed = 0;

        HashCombine(seed, key.computeShader);
        HashCombine(seed, key.setLayouts.size());

        for (VkDescriptorSetLayout layout : key.setLayouts)
        {
            HashCombine(seed, layout);
        }

        HashCombine(seed, key.pushConstantSize);
        HashCombine(seed, key.pushConstantStages);

        return seed;
    }

    PipelineManager::ComputePipelineKey PipelineManager::MakeComputeKey(
        const ComputePipelineDesc& desc)
    {
        ComputePipelineKey key{};

        key.computeShader = NormalizePath(desc.computeShader);

        if (desc.setLayoutCount > 0)
        {
            key.setLayouts.assign(
                desc.pSetLayouts,
                desc.pSetLayouts + desc.setLayoutCount);
        }

        key.pushConstantSize = desc.pushConstantSize;
        key.pushConstantStages =
            desc.pushConstantSize > 0 ? desc.pushConstantStages : 0;

        return key;
    }

    void PipelineManager::ValidateComputeDesc(
        const ComputePipelineDesc& desc)
    {
        if (desc.computeShader.empty())
        {
            OSIRIS_ERROR("PipelineManager: a compute shader is required");
        }

        if (desc.setLayoutCount > 0 &&
            desc.pSetLayouts == nullptr)
        {
            OSIRIS_ERROR("PipelineManager: setLayoutCount is non-zero but pSetLayouts is null");
        }

        for (uint32_t i = 0; i < desc.setLayoutCount; ++i)
        {
            if (desc.pSetLayouts[i] == VK_NULL_HANDLE)
            {
                OSIRIS_ERROR("PipelineManager: descriptor set layout is null");
            }
        }

        if ((desc.pushConstantSize % 4U) != 0)
        {
            OSIRIS_ERROR("PipelineManager: pushConstantSize must be a multiple of four bytes");
        }

        if (desc.pushConstantSize > 0 &&
            desc.pushConstantStages == 0)
        {
            OSIRIS_ERROR("PipelineManager: Push constants require at least one shader stage");
        }
    }

    void PipelineManager::ValidateDesc(
        const PipelineDesc& desc)
    {
        if (desc.vertexShader.empty())
        {
            OSIRIS_ERROR("PipelineManager: a vertex shader is required");
        }

        if (desc.colorAttachment)
        {
            if (desc.colorFormat == VK_FORMAT_UNDEFINED)
            {
                OSIRIS_ERROR("PipelineManager: Color attachment enabled but color format is VK_FORMAT_UNDEFINED");

            }

            if (desc.fragmentShader.empty())
            {
                OSIRIS_ERROR("PipelineManager: Color attachment enabled but no fragment shader was supplied");
            }
        }

        if (desc.depthAttachment &&
            desc.depthFormat == VK_FORMAT_UNDEFINED)
        {
            OSIRIS_ERROR("PipelineManager: depth attachment enabled but depthformat is VK_FORMAT_UNDEFINED");
        }

        if (!desc.depthAttachment &&
            (desc.depthTest || desc.depthWrite))
        {
            OSIRIS_ERROR("PipelineManager: depth test/write requires a depth attachment");
        }

        if (!desc.colorAttachment &&
            !desc.depthAttachment)
        {
            OSIRIS_ERROR("PipelineManager: pipeline has neither a color nor a depth attachment");
        }

        if (desc.setLayoutCount > 0 &&
            desc.pSetLayouts == nullptr)
        {
            OSIRIS_ERROR("PipelineManager: setLayoutCount is non-zero but pSetLayouts is null");
        }

        for (uint32_t i = 0; i < desc.setLayoutCount; ++i)
        {
            if (desc.pSetLayouts[i] == VK_NULL_HANDLE)
            {
                OSIRIS_ERROR("PipelineManager: descriptor set layout is null");
            }
        }

        if ((desc.pushConstantSize % 4U) != 0)
        {
            OSIRIS_ERROR("PipelineManager: pushConstantSize must be a multiple of four bytes");
        }

        if (desc.pushConstantSize > 0 &&
            desc.pushConstantStages == 0)
        {
            OSIRIS_ERROR("PipelineManager: Push constants require at least one shader stage");
        }

        if (desc.samples == 0)
        {
            OSIRIS_ERROR("PipelineManager: invalide sample count");
        }
    }

    PipelineManager::PipelineEntry
    PipelineManager::CreatePipeline(
        const PipelineKey& key) const
    {
        PipelineEntry entry{};
        VkShaderModule vertexModule = VK_NULL_HANDLE;
        VkShaderModule fragmentModule = VK_NULL_HANDLE;

        try
        {
            entry.layout = CreatePipelineLayout(key);
            vertexModule = LoadShaderModule(key.vertexShader);

            if (!key.fragmentShader.empty())
            {
                fragmentModule =
                    LoadShaderModule(key.fragmentShader);
            }

            std::array<VkPipelineShaderStageCreateInfo, 2>
                shaderStages{};

            uint32_t shaderStageCount = 0;

            shaderStages[shaderStageCount++] = {
                .sType =
                    VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .stage = VK_SHADER_STAGE_VERTEX_BIT,
                .module = vertexModule,
                .pName = "main",
                .pSpecializationInfo = nullptr
            };

            if (fragmentModule != VK_NULL_HANDLE)
            {
                shaderStages[shaderStageCount++] = {
                    .sType =
                        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                    .pNext = nullptr,
                    .flags = 0,
                    .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                    .module = fragmentModule,
                    .pName = "main",
                    .pSpecializationInfo = nullptr
                };
            }

            const VkVertexInputBindingDescription bindingDescription{
                .binding = 0,
                .stride = sizeof(Vertex),
                .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
            };

            const std::array<
                VkVertexInputAttributeDescription,
                4> attributeDescriptions{
                VkVertexInputAttributeDescription{
                    .location = 0,
                    .binding = 0,
                    .format = VK_FORMAT_R32G32B32_SFLOAT,
                    .offset = static_cast<uint32_t>(
                        offsetof(Vertex, Position))
                },
                VkVertexInputAttributeDescription{
                    .location = 1,
                    .binding = 0,
                    .format = VK_FORMAT_R32G32B32_SFLOAT,
                    .offset = static_cast<uint32_t>(
                        offsetof(Vertex, Normal))
                },
                VkVertexInputAttributeDescription{
                    .location = 2,
                    .binding = 0,
                    .format = VK_FORMAT_R32G32_SFLOAT,
                    .offset = static_cast<uint32_t>(
                        offsetof(Vertex, TexCoord))
                },
                    VkVertexInputAttributeDescription{
                        .location = 3,
                        .binding = 0,
                        .format = VK_FORMAT_R32G32B32A32_SFLOAT,
                        .offset = static_cast<uint32_t>(
                            offsetof(Vertex, Tangent))
                    },
            };

            const VkPipelineVertexInputStateCreateInfo
                vertexInputState{
                    .sType =
                        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
                    .pNext = nullptr,
                    .flags = 0,
                    .vertexBindingDescriptionCount =
                        key.vertexInput ? 1U : 0U,
                    .pVertexBindingDescriptions =
                        key.vertexInput ? &bindingDescription : nullptr,
                    .vertexAttributeDescriptionCount =
                        key.vertexInput
                            ? static_cast<uint32_t>(
                                attributeDescriptions.size())
                            : 0U,
                    .pVertexAttributeDescriptions =
                        key.vertexInput
                            ? attributeDescriptions.data()
                            : nullptr
                };

            const VkPipelineInputAssemblyStateCreateInfo
                inputAssembly{
                    .sType =
                        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
                    .pNext = nullptr,
                    .flags = 0,
                    .topology = key.topology,
                    .primitiveRestartEnable = VK_FALSE
                };

            // Viewport, scissor, and shadow depth bias values are supplied dynamically.
            const VkPipelineViewportStateCreateInfo viewportState{
                .sType =
                    VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .viewportCount = 1,
                .pViewports = nullptr,
                .scissorCount = 1,
                .pScissors = nullptr
            };

            const VkPipelineRasterizationStateCreateInfo
                rasterizationState{
                    .sType =
                        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                    .pNext = nullptr,
                    .flags = 0,
                    .depthClampEnable = VK_FALSE,
                    .rasterizerDiscardEnable = VK_FALSE,
                    .polygonMode = key.polygonMode,
                    .cullMode = key.cullMode,
                    .frontFace = key.frontFace,
                    .depthBiasEnable =
                        key.depthBias ? VK_TRUE : VK_FALSE,
                    .depthBiasConstantFactor =
                        key.depthBiasConstant,
                    .depthBiasClamp = key.depthBiasClamp,
                    .depthBiasSlopeFactor =
                        key.depthBiasSlope,
                    .lineWidth = 1.0f
                };

            const VkPipelineMultisampleStateCreateInfo
                multisampleState{
                    .sType =
                        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
                    .pNext = nullptr,
                    .flags = 0,
                    .rasterizationSamples = key.samples,
                    .sampleShadingEnable = VK_FALSE,
                    .minSampleShading = 1.0f,
                    .pSampleMask = nullptr,
                    .alphaToCoverageEnable = VK_FALSE,
                    .alphaToOneEnable = VK_FALSE
                };

            const VkPipelineDepthStencilStateCreateInfo
                depthStencilState{
                    .sType =
                        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
                    .pNext = nullptr,
                    .flags = 0,
                    .depthTestEnable =
                        key.depthTest ? VK_TRUE : VK_FALSE,
                    .depthWriteEnable =
                        key.depthWrite ? VK_TRUE : VK_FALSE,
                    .depthCompareOp = key.depthCompareOp,
                    .depthBoundsTestEnable = VK_FALSE,
                    .stencilTestEnable = VK_FALSE,
                    .front = {},
                    .back = {},
                    .minDepthBounds = 0.0f,
                    .maxDepthBounds = 1.0f
                };

            const VkPipelineColorBlendAttachmentState
                colorBlendAttachment{
                    .blendEnable = key.alphaBlend ? VK_TRUE : VK_FALSE,
                    .srcColorBlendFactor =
                        key.alphaBlend
                            ? VK_BLEND_FACTOR_SRC_ALPHA
                            : VK_BLEND_FACTOR_ONE,
                    .dstColorBlendFactor =
                        key.alphaBlend
                            ? VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA
                            : VK_BLEND_FACTOR_ZERO,
                    .colorBlendOp = VK_BLEND_OP_ADD,
                    .srcAlphaBlendFactor =
                        VK_BLEND_FACTOR_ONE,
                    .dstAlphaBlendFactor =
                        key.alphaBlend
                            ? VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA
                            : VK_BLEND_FACTOR_ZERO,
                    .alphaBlendOp = VK_BLEND_OP_ADD,
                    .colorWriteMask =
                        VK_COLOR_COMPONENT_R_BIT |
                        VK_COLOR_COMPONENT_G_BIT |
                        VK_COLOR_COMPONENT_B_BIT |
                        VK_COLOR_COMPONENT_A_BIT
                };

            const VkPipelineColorBlendStateCreateInfo
                colorBlendState{
                    .sType =
                        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                    .pNext = nullptr,
                    .flags = 0,
                    .logicOpEnable = VK_FALSE,
                    .logicOp = VK_LOGIC_OP_COPY,
                    .attachmentCount =
                        key.colorAttachment ? 1U : 0U,
                    .pAttachments =
                        key.colorAttachment
                            ? &colorBlendAttachment
                            : nullptr,
                    .blendConstants = {
                        0.0f,
                        0.0f,
                        0.0f,
                        0.0f
                    }
                };

            constexpr std::array<VkDynamicState, 3>
                dynamicStates{
                    VK_DYNAMIC_STATE_VIEWPORT,
                    VK_DYNAMIC_STATE_SCISSOR,
                    VK_DYNAMIC_STATE_DEPTH_BIAS
                };

            const VkPipelineDynamicStateCreateInfo
                dynamicState{
                    .sType =
                        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
                    .pNext = nullptr,
                    .flags = 0,
                    .dynamicStateCount =
                        static_cast<uint32_t>(
                            dynamicStates.size()),
                    .pDynamicStates = dynamicStates.data()
                };

            VkFormat colorFormat = key.colorFormat;

            const VkPipelineRenderingCreateInfo renderingInfo{
                .sType =
                    VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
                .pNext = nullptr,
                .viewMask = 0,
                .colorAttachmentCount =
                    key.colorAttachment ? 1U : 0U,
                .pColorAttachmentFormats =
                    key.colorAttachment
                        ? &colorFormat
                        : nullptr,
                .depthAttachmentFormat =
                    key.depthAttachment
                        ? key.depthFormat
                        : VK_FORMAT_UNDEFINED,
                .stencilAttachmentFormat =
                    VK_FORMAT_UNDEFINED
            };

            const VkGraphicsPipelineCreateInfo pipelineInfo{
                .sType =
                    VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
                .pNext = &renderingInfo,
                .flags = 0,
                .stageCount = shaderStageCount,
                .pStages = shaderStages.data(),
                .pVertexInputState = &vertexInputState,
                .pInputAssemblyState = &inputAssembly,
                .pTessellationState = nullptr,
                .pViewportState = &viewportState,
                .pRasterizationState =
                    &rasterizationState,
                .pMultisampleState = &multisampleState,
                .pDepthStencilState =
                    key.depthAttachment
                        ? &depthStencilState
                        : nullptr,
                .pColorBlendState = &colorBlendState,
                .pDynamicState = &dynamicState,
                .layout = entry.layout,
                .renderPass = VK_NULL_HANDLE,
                .subpass = 0,
                .basePipelineHandle = VK_NULL_HANDLE,
                .basePipelineIndex = -1
            };

            const VkResult result = vkCreateGraphicsPipelines(
                m_Device,
                m_PipelineCache,
                1,
                &pipelineInfo,
                m_Allocator,
                &entry.pipeline);

            if (result != VK_SUCCESS)
            {
                OSIRIS_ERROR("PipelineManager: vkCreateGraphicsPipelines failed with result: {}",
                             std::to_string(static_cast<int32_t>(result)));
            }

            vkDestroyShaderModule(
                m_Device,
                vertexModule,
                m_Allocator);

            vertexModule = VK_NULL_HANDLE;

            if (fragmentModule != VK_NULL_HANDLE)
            {
                vkDestroyShaderModule(
                    m_Device,
                    fragmentModule,
                    m_Allocator);

                fragmentModule = VK_NULL_HANDLE;
            }

            return entry;
        }
        catch (...)
        {
            if (vertexModule != VK_NULL_HANDLE)
            {
                vkDestroyShaderModule(
                    m_Device,
                    vertexModule,
                    m_Allocator);
            }

            if (fragmentModule != VK_NULL_HANDLE)
            {
                vkDestroyShaderModule(
                    m_Device,
                    fragmentModule,
                    m_Allocator);
            }

            if (entry.pipeline != VK_NULL_HANDLE)
            {
                vkDestroyPipeline(
                    m_Device,
                    entry.pipeline,
                    m_Allocator);
            }

            if (entry.layout != VK_NULL_HANDLE)
            {
                vkDestroyPipelineLayout(
                    m_Device,
                    entry.layout,
                    m_Allocator);
            }

            OSIRIS_ERROR("PipelineManager: Destroying ShaderModule for vert/frag, pipeline and pipeline layout");
        }
    }

    VkPipelineLayout PipelineManager::CreatePipelineLayout(
        const PipelineKey& key) const
    {
        VkPushConstantRange pushConstantRange{
            .stageFlags = key.pushConstantStages,
            .offset = 0,
            .size = key.pushConstantSize
        };

        const VkPipelineLayoutCreateInfo layoutInfo{
            .sType =
                VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .setLayoutCount =
                static_cast<uint32_t>(key.setLayouts.size()),
            .pSetLayouts =
                key.setLayouts.empty()
                    ? nullptr
                    : key.setLayouts.data(),
            .pushConstantRangeCount =
                key.pushConstantSize > 0 ? 1U : 0U,
            .pPushConstantRanges =
                key.pushConstantSize > 0
                    ? &pushConstantRange
                    : nullptr
        };

        VkPipelineLayout layout = VK_NULL_HANDLE;

        const VkResult result = vkCreatePipelineLayout(
            m_Device,
            &layoutInfo,
            m_Allocator,
            &layout);

        if (result != VK_SUCCESS)
        {
            OSIRIS_ERROR("PipelineManager: vkCreatePipelineLayout failed with result {}",
                         std::to_string(static_cast<int32_t>(result)));
        }

        return layout;
    }

    VkPipelineLayout PipelineManager::CreateComputePipelineLayout(
        const ComputePipelineKey& key) const
    {
        VkPushConstantRange pushConstantRange{
            .stageFlags = key.pushConstantStages,
            .offset = 0,
            .size = key.pushConstantSize
        };

        const VkPipelineLayoutCreateInfo layoutInfo{
            .sType =
                VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .setLayoutCount =
                static_cast<uint32_t>(key.setLayouts.size()),
            .pSetLayouts =
                key.setLayouts.empty()
                    ? nullptr
                    : key.setLayouts.data(),
            .pushConstantRangeCount =
                key.pushConstantSize > 0 ? 1U : 0U,
            .pPushConstantRanges =
                key.pushConstantSize > 0
                    ? &pushConstantRange
                    : nullptr
        };

        VkPipelineLayout layout = VK_NULL_HANDLE;

        const VkResult result = vkCreatePipelineLayout(
            m_Device,
            &layoutInfo,
            m_Allocator,
            &layout);

        if (result != VK_SUCCESS)
        {
            OSIRIS_ERROR("PipelineManager: vkCreatePipelineLayout (compute) failed with result {}",
                         std::to_string(static_cast<int32_t>(result)));
        }

        return layout;
    }

    PipelineManager::PipelineEntry
    PipelineManager::CreateComputePipelineEntry(
        const ComputePipelineKey& key) const
    {
        PipelineEntry entry{};
        VkShaderModule computeModule = VK_NULL_HANDLE;

        try
        {
            entry.layout = CreateComputePipelineLayout(key);
            computeModule = LoadShaderModule(key.computeShader);

            const VkPipelineShaderStageCreateInfo shaderStage{
                .sType =
                    VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                .module = computeModule,
                .pName = "main",
                .pSpecializationInfo = nullptr
            };

            const VkComputePipelineCreateInfo pipelineInfo{
                .sType =
                    VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .stage = shaderStage,
                .layout = entry.layout,
                .basePipelineHandle = VK_NULL_HANDLE,
                .basePipelineIndex = -1
            };

            const VkResult result = vkCreateComputePipelines(
                m_Device,
                m_PipelineCache,
                1,
                &pipelineInfo,
                m_Allocator,
                &entry.pipeline);

            if (result != VK_SUCCESS)
            {
                OSIRIS_ERROR("PipelineManager: vkCreateComputePipelines failed with result: {}",
                             std::to_string(static_cast<int32_t>(result)));
            }

            vkDestroyShaderModule(
                m_Device,
                computeModule,
                m_Allocator);

            computeModule = VK_NULL_HANDLE;

            return entry;
        }
        catch (...)
        {
            if (computeModule != VK_NULL_HANDLE)
            {
                vkDestroyShaderModule(
                    m_Device,
                    computeModule,
                    m_Allocator);
            }

            if (entry.pipeline != VK_NULL_HANDLE)
            {
                vkDestroyPipeline(
                    m_Device,
                    entry.pipeline,
                    m_Allocator);
            }

            if (entry.layout != VK_NULL_HANDLE)
            {
                vkDestroyPipelineLayout(
                    m_Device,
                    entry.layout,
                    m_Allocator);
            }

            OSIRIS_ERROR("PipelineManager: Destroying compute shader module, pipeline and pipeline layout");
        }
    }

    VkShaderModule PipelineManager::LoadShaderModule(
        std::string_view path) const
    {
        const std::vector<char> bytes = ReadBinaryFile(path);

        if ((bytes.size() % sizeof(uint32_t)) != 0)
        {
            OSIRIS_ERROR("PipelineManager: SPIR-V file size is not a multiple of four bytes {}", std::string(path));
        }

        if (bytes.size() < sizeof(uint32_t))
        {
            OSIRIS_ERROR("PipelineManager: SPIR-V file is too small {}", std::string(path));
        }

        // Copying to uint32_t storage avoids relying on the alignment of
        // vector<char>::data().
        std::vector<uint32_t> code(
            bytes.size() / sizeof(uint32_t));

        std::memcpy(
            code.data(),
            bytes.data(),
            bytes.size());

        constexpr uint32_t spirvMagic = 0x07230203U;

        if (code.front() != spirvMagic)
        {
            OSIRIS_ERROR("PipelineManager: invalid SPIR-V magic number");
        }

        const VkShaderModuleCreateInfo shaderInfo{
            .sType =
                VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .codeSize = bytes.size(),
            .pCode = code.data()
        };

        VkShaderModule module = VK_NULL_HANDLE;

        const VkResult result = vkCreateShaderModule(
            m_Device,
            &shaderInfo,
            m_Allocator,
            &module);

        if (result != VK_SUCCESS)
        {
            OSIRIS_ERROR("PipelineManager: vkCreateShaderModule failed for {} with result {}", std::string(path),
                         std::to_string(static_cast<int32_t>(result)));
        }

        return module;
    }
}
