//
// Created by Debrkey on 03/08/2026.
//

#include "RenderGraph.h"

#include <queue>

#include "core/Log.h"

namespace Osiris {
    struct VulkanResourceState {
        VkImageLayout        layout;
        VkAccessFlags        accessMask;
        VkPipelineStageFlags stageFlags;
    };

    static VulkanResourceState GetVulkanState(ResourceState state) {
        switch (state) {
            case ResourceState::Undefined:
                return {
                    VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_ACCESS_NONE,
                    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
                };
            case ResourceState::ColorWrite:
                return {
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                };
            case ResourceState::DepthWrite:
                return {
                    VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                    VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT
                };
            case ResourceState::ShaderRead:
                return {
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_ACCESS_SHADER_READ_BIT,
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                };
            case ResourceState::Present:
                return {
                    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                    VK_ACCESS_NONE,
                    VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT
                };
            case ResourceState::ComputeRead:
                return {
                    VK_IMAGE_LAYOUT_GENERAL,
                    VK_ACCESS_SHADER_READ_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
                };
            case ResourceState::ComputeWrite:
                return {
                    VK_IMAGE_LAYOUT_GENERAL,
                    VK_ACCESS_SHADER_WRITE_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
                };
            default:
                return {
                    VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_ACCESS_NONE,
                    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
                };
        }
    }

    RenderPass & RenderPass::Read(ResourceAccess access) {
        reads.push_back(access);
        return *this;
    }

    RenderPass & RenderPass::Write(ResourceAccess access) {
        writes.push_back(access);
        return *this;
    }

    RenderPass & RenderPass::SetExecute(std::function<void(VkCommandBuffer)> callback) {
        execute = callback;
        return *this;
    }

    RenderPass & RenderGraph::AddPass(const std::string &name, PassType type) {
        RenderPass pass;
        pass.name = name;
        pass.type = type;
        m_Passes.push_back(pass);
        return m_Passes.back();
    }

    void RenderGraph::Compile() {
        std::unordered_map<uint32_t, int> producer;
        std::vector<int> inDegree(m_Passes.size(), 0);
        std::vector<std::vector<int>> adjacency(m_Passes.size());

        // Loop 1 — build producer map from ALL writes first
        for (int i = 0; i < static_cast<int>(m_Passes.size()); i++) {
            for (auto& write : m_Passes[i].writes) {
                producer[write.texture.id] = i;
            }
        }

        // Loop 2 — build dependency edges from ALL reads
        for (int i = 0; i < static_cast<int>(m_Passes.size()); i++) {
            for (auto& read : m_Passes[i].reads) {
                auto it = producer.find(read.texture.id);
                if (it != producer.end()) {
                    int producerIndex = it->second;
                    adjacency[producerIndex].push_back(i);
                    inDegree[i]++;
                }
            }
        }

        std::queue<int> zeroInDegree;
        for (int i = 0; i < static_cast<int>(m_Passes.size()); i++) {
            if (inDegree[i] == 0) {
                zeroInDegree.push(i);
            }
        }

        m_SortedPasses.clear();

        while(!zeroInDegree.empty()) {
            int current = zeroInDegree.front();
            zeroInDegree.pop();
            m_SortedPasses.push_back(current);

            for (int dependent : adjacency[current]) {
                inDegree[dependent]--;
                if (inDegree[dependent] == 0) {
                    zeroInDegree.push(dependent);
                }
            }
        }
        if (m_SortedPasses.size() != m_Passes.size()) {
            OSIRIS_ERROR("RenderGraph: circular dependency detected!");
            m_SortedPasses.clear();
        }

        OSIRIS_INFO("RenderGraph compiled {} passes:", m_SortedPasses.size());
        for (int i : m_SortedPasses) {
            OSIRIS_INFO("  Pass {}: {}", i, m_Passes[i].name);
        }
    }

void RenderGraph::Execute(VkCommandBuffer cmd) {
    for (int passIndex : m_SortedPasses) {
        RenderPass& pass = m_Passes[passIndex];

        std::vector<VkImageMemoryBarrier> barriers;

        // Collect barriers for all reads
        for (auto& access : pass.reads) {
            ResourceState currentState = m_ResourceStates[access.texture.id];
            ResourceState requiredState = access.state;

            VkImageAspectFlags aspect = (access.state == ResourceState::DepthWrite)
            ? VK_IMAGE_ASPECT_DEPTH_BIT
            : VK_IMAGE_ASPECT_COLOR_BIT;

            if (currentState != requiredState) {
                VulkanResourceState src = GetVulkanState(currentState);
                VulkanResourceState dst = GetVulkanState(requiredState);

                VkImageMemoryBarrier barrier = {
                    .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                    .srcAccessMask       = src.accessMask,
                    .dstAccessMask       = dst.accessMask,
                    .oldLayout           = src.layout,
                    .newLayout           = dst.layout,
                    .image               = m_Images[access.texture.id],
                    .subresourceRange    = {
                        .aspectMask      = aspect,
                        .baseMipLevel    = 0,
                        .levelCount      = 1,
                        .baseArrayLayer  = 0,
                        .layerCount      = 1,
                    },
                };
                barriers.push_back(barrier);
                m_ResourceStates[access.texture.id] = requiredState;
            }
        }

        // Collect barriers for all writes
        for (auto& access : pass.writes) {
            ResourceState currentState = m_ResourceStates[access.texture.id];
            ResourceState requiredState = access.state;

            VkImageAspectFlags aspect = (access.state == ResourceState::DepthWrite)
            ? VK_IMAGE_ASPECT_DEPTH_BIT
            : VK_IMAGE_ASPECT_COLOR_BIT;

            if (currentState != requiredState) {
                VulkanResourceState src = GetVulkanState(currentState);
                VulkanResourceState dst = GetVulkanState(requiredState);

                VkImageMemoryBarrier barrier = {
                    .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                    .srcAccessMask       = src.accessMask,
                    .dstAccessMask       = dst.accessMask,
                    .oldLayout           = src.layout,
                    .newLayout           = dst.layout,
                    .image               = m_Images[access.texture.id],
                    .subresourceRange    = {
                        .aspectMask      = aspect,
                        .baseMipLevel    = 0,
                        .levelCount      = 1,
                        .baseArrayLayer  = 0,
                        .layerCount      = 1,
                    },
                };
                barriers.push_back(barrier);
                m_ResourceStates[access.texture.id] = requiredState;
            }
        }

        // Submit all barriers for this pass in one call
        if (!barriers.empty()) {
            // Determine src and dst stage from all barriers
            VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

            for (auto& barrier : barriers) {
                // Find the widest stage needed
                for (auto& access : pass.reads) {
                    VulkanResourceState dst = GetVulkanState(access.state);
                    dstStage |= dst.stageFlags;
                }
                for (auto& access : pass.writes) {
                    VulkanResourceState dst = GetVulkanState(access.state);
                    dstStage |= dst.stageFlags;
                    VulkanResourceState src = GetVulkanState(m_ResourceStates[access.texture.id]);
                    srcStage |= src.stageFlags;
                }
            }

            vkCmdPipelineBarrier(cmd,
                srcStage, dstStage,
                0,
                0, nullptr,
                0, nullptr,
                static_cast<uint32_t>(barriers.size()), barriers.data());
        }

        // Execute the pass
        if (pass.execute) {
            pass.execute(cmd);
        }
    }
}

    void RenderGraph::Reset() {
        m_Passes.clear();
        m_SortedPasses.clear();
    }

    // TODO: Remove VkImage from RenderGraph, route through IRHI::TransitionTexture
    void RenderGraph::ImportTexture(RGTexture texture, VkImage image, ResourceState initialState) {
        m_ResourceStates[texture.id] = initialState;
        m_Images[texture.id] = image;
    }
} // Osiris