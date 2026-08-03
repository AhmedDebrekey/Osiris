//
// Created by Debrkey on 03/08/2026.
//

#include "RenderGraph.h"

#include <queue>

#include "core/Log.h"

namespace Osiris {
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
            if (m_Passes[passIndex].execute) {
                m_Passes[passIndex].execute(cmd);
            }
        }
    }

    void RenderGraph::Reset() {
        m_Passes.clear();
        m_SortedPasses.clear();
    }

} // Osiris