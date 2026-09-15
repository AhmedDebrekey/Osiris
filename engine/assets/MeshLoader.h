//
// Created by Debreky on 21/07/2026.
//

#ifndef OSIRIS_MESHLOADER_H
#define OSIRIS_MESHLOADER_H
#include "renderer/MeshType.h"
#include "rhi/RHI.h"
#include <optional>
#include <string>
#include <vector>
#include "animation/Animation.h"

namespace Osiris {
    struct GltfNode {
        std::string name;
        glm::mat4 localTransform{1.0f};
        std::vector<MeshPrimitive> primitives;
        std::vector<std::size_t> childIndices;
        std::optional<std::size_t> parentIndex;
        uint32_t sourceIndex = 0;
        std::optional<std::size_t> skinIndex;
    };

    struct GltfPrimitiveInstance {
        MeshPrimitive primitive;
        glm::mat4 transform{1.0f};
    };

    class MeshLoader {
        public:
            static std::vector<GltfNode> LoadFromGLTF(const std::string& path, IRHI* rhi,
                std::shared_ptr<const AnimationAsset>* animation = nullptr);
            static void ClearCache(IRHI* rhi);
            static std::vector<GltfPrimitiveInstance> FlattenPrimitives(const std::vector<GltfNode>& nodes);
            static Mesh CreatePlane(float width, float height, IRHI* rhi);
            static Mesh CreateBox(const glm::vec3& halfExtents, IRHI* rhi);
            static void GenerateTangents(std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);
    };
} // Osiris

#endif //OSIRIS_MESHLOADER_H
