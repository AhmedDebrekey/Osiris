//
// Created by Debreky on 21/07/2026.
//

#ifndef OSIRIS_MESHLOADER_H
#define OSIRIS_MESHLOADER_H
#include "renderer/MeshType.h"
#include "rhi/RHI.h"
#include <string>
#include <vector>

namespace Osiris {
    class MeshLoader {
        public:
            static std::vector<MeshPrimitive> LoadFromGLTF(const std::string& path, IRHI* rhi);
            static Mesh CreatePlane(float width, float height, IRHI* rhi);
            static Mesh CreateBox(const glm::vec3& halfExtents, IRHI* rhi);
            static void GenerateTangents(std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);
    };
} // Osiris

#endif //OSIRIS_MESHLOADER_H