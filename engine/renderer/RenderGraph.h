//
// Created by Debreky on 03/08/2026.
//

#ifndef OSIRIS_RENDERGRAPH_H
#define OSIRIS_RENDERGRAPH_H
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>
#include <string>

// TODO: Replace VkCommandBuffer with CommandBuffer abstraction
#include <vulkan/vulkan.h>

namespace Osiris {
    class RenderGraph;

    enum ResourceState {
        Undefined,
        ColorWrite,
        DepthWrite,
        ShaderRead,
        Present,
        ComputeRead,
        ComputeWrite,
    };

    struct RGTexture {
        uint32_t id = UINT32_MAX;
        bool IsValid() const { return id != UINT32_MAX; }
    };

    enum PassType {
        Graphics,
        Compute,
    };

    struct ResourceAccess {
        RGTexture     texture;
        ResourceState state;
    };

    class RenderPass {
    public:
        RenderPass& Read(ResourceAccess access);
        RenderPass& Write(ResourceAccess access);
        RenderPass& SetExecute(std::function<void(VkCommandBuffer)> callback);
    private:
        std::string                  name;
        PassType                     type = PassType::Graphics;
        std::vector<ResourceAccess>  reads;
        std::vector<ResourceAccess>  writes;
        std::function<void(VkCommandBuffer)> execute;

        friend RenderGraph;
    };

    class RenderGraph {
    public:
        RenderPass& AddPass(const std::string& name, PassType type);
        void Compile();
        void Execute(VkCommandBuffer cmd);


        void Reset();

        void ImportTexture(RGTexture texture, VkImage image, ResourceState initialState);
    private:

        std::vector<RenderPass> m_Passes;
        std::vector<int>        m_SortedPasses;

        std::unordered_map<uint32_t, ResourceState> m_ResourceStates;   // resourceId → current state
        std::unordered_map<uint32_t, VkImage>       m_Images;           // resourceId → VkImage

        // Compile()/Execute() scratch buffers, kept as members and .clear()'d (not freshly
        // constructed) each call: this graph is Reset()+rebuilt from scratch for almost every
        // individual pass (see VulkanRHI's bloom/forward/post-process call sites), often 20+
        // times a frame for a graph of just one node, so a fresh heap allocation per container
        // per call adds up fast, especially under a debug allocator/checked-iterator STL.
        std::unordered_map<uint32_t, int> m_CompileProducer;
        std::vector<int>                  m_CompileInDegree;
        std::vector<std::vector<int>>     m_CompileAdjacency;
        std::vector<int>                  m_CompileQueue;
        std::vector<VkImageMemoryBarrier> m_ExecuteBarriers;
    };
} // Osiris

#endif //OSIRIS_RENDERGRAPH_H