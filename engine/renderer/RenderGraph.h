//
// Created by Debreky on 03/08/2026.
//

#ifndef OSIRIS_RENDERGRAPH_H
#define OSIRIS_RENDERGRAPH_H
#include <cstdint>
#include <functional>
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

    private:
        std::vector<RenderPass> m_Passes;
        std::vector<int> m_SortedPasses;
    };
} // Osiris

#endif //OSIRIS_RENDERGRAPH_H