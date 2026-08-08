//
// Created by ahtal on 21/07/2026.
//

#include "MeshLoader.h"
#include "fastgltf/core.hpp"
#include "fastgltf/types.hpp"
#include "fastgltf/tools.hpp"
#include "core/Log.h"
#include "fastgltf/glm_element_traits.hpp"

namespace Osiris {
    Mesh MeshLoader::LoadFromGLTF(const std::string &path, IRHI *rhi) {
        fastgltf::Parser parser;
        fastgltf::GltfDataBuffer data;

        if (!data.loadFromFile(path)) {
            OSIRIS_ERROR("Failed to load glTF file: {}", path);
            return Mesh{};
        }

        auto asset = parser.loadGltf(&data,
            std::filesystem::path(path).parent_path(),
            fastgltf::Options::LoadExternalBuffers);

        if (asset.error() != fastgltf::Error::None) {
            OSIRIS_ERROR("Failed to parse glTF file: {}", path);
            return Mesh{};
        }

        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        AABB bounds;

        // Get the first mesh's first primitive
        auto& mesh = asset->meshes[0];
        auto& primitive = mesh.primitives[0];

        OSIRIS_INFO("Has indices: {}", primitive.indicesAccessor.has_value());
        if (primitive.indicesAccessor.has_value()) {
            OSIRIS_INFO("Indices accessor index: {}", primitive.indicesAccessor.value());
        }


        // Extract positions
        auto posIt = std::find_if(primitive.attributes.begin(), primitive.attributes.end(),
            [](const auto& attr) { return attr.first == "POSITION"; });
        if (posIt != primitive.attributes.end()) {
            auto& accessor = asset->accessors[posIt->second];
            vertices.resize(accessor.count);
            std::size_t i = 0;
            fastgltf::iterateAccessor<glm::vec3>(asset.get(), accessor, [&](glm::vec3 pos) {
                vertices[i++].Position = pos;
            });
        }

        for (const auto& vertex : vertices) {
            bounds.min = glm::min(bounds.min, vertex.Position);
            bounds.max = glm::max(bounds.max, vertex.Position);
        }

        // Extract indices
        if (primitive.indicesAccessor.has_value()) {
            auto& accessor = asset->accessors[primitive.indicesAccessor.value()];
            indices.resize(accessor.count);
            std::size_t i = 0;
            fastgltf::iterateAccessor<std::uint32_t>(asset.get(), accessor, [&](std::uint32_t index) {
                indices[i++] = index;
            });
        } else {
            // No index buffer — generate sequential indices
            indices.resize(vertices.size());
            for (uint32_t i = 0; i < vertices.size(); i++) {
                indices[i] = i;
            }
        }

        // Extract normals
        auto normIt = std::find_if(primitive.attributes.begin(), primitive.attributes.end(),
            [](const auto& attr) { return attr.first == "NORMAL"; });
        if (normIt != primitive.attributes.end()) {
            auto& accessor = asset->accessors[normIt->second];
            std::size_t i = 0;
            fastgltf::iterateAccessor<glm::vec3>(asset.get(), accessor, [&](glm::vec3 norm) {
                vertices[i++].Normal = norm;
            });
        }

        // Extract UVs
        auto uvIt = std::find_if(primitive.attributes.begin(), primitive.attributes.end(),
            [](const auto& attr) { return attr.first == "TEXCOORD_0"; });
        if (uvIt != primitive.attributes.end()) {
            auto& accessor = asset->accessors[uvIt->second];
            std::size_t i = 0;
            fastgltf::iterateAccessor<glm::vec2>(asset.get(), accessor, [&](glm::vec2 uv) {
                vertices[i++].TexCoord = uv;
            });
        }

        // Extract Tangents
        auto tangentIt = std::find_if(primitive.attributes.begin(), primitive.attributes.end(),
            [](const auto& attr) { return attr.first == "TANGENT"; });
        bool hasTangets = (tangentIt != primitive.attributes.end());
        if (!hasTangets) {
            GenerateTangents(vertices, indices);
        } else {
            OSIRIS_INFO("Found Tangent: {}", tangentIt->second);
            auto& accessor = asset->accessors[tangentIt->second];
            std::size_t i = 0;
            fastgltf::iterateAccessor<glm::vec4>(asset.get(), accessor, [&](glm::vec4 tangent) {
                vertices[i++].Tangent = tangent;
            });
        }

        OSIRIS_INFO("Vertices: {}", vertices.size());
        OSIRIS_INFO("Indices: {}", indices.size());

        if (vertices.empty() || indices.empty()) {
            OSIRIS_ERROR("Failed to extract mesh data from glTF");
            return Mesh{};
        }

        // Upload to GPU
        BufferDesc vertexBufferDesc = {
            .size       = sizeof(Vertex) * vertices.size(),
            .usage      = BufferUsage::Vertex,
            .cpuVisible = false,
        };

        BufferDesc indexBufferDesc = {
            .size       = sizeof(uint32_t) * indices.size(),
            .usage      = BufferUsage::Index,
            .cpuVisible = false,
        };

        BufferHandle vertexBuffer = rhi->CreateBuffer(vertexBufferDesc);
        rhi->UploadBufferData(vertexBuffer, vertices.data(), sizeof(Vertex) * vertices.size());

        BufferHandle indexBuffer = rhi->CreateBuffer(indexBufferDesc);
        rhi->UploadBufferData(indexBuffer, indices.data(), sizeof(uint32_t) * indices.size());

        return Mesh {
            .vertexBuffer = vertexBuffer,
            .indexBuffer  = indexBuffer,
            .vertexCount  = static_cast<uint32_t>(vertices.size()),
            .indexCount   = static_cast<uint32_t>(indices.size()),
            .bounds       = bounds,
        };
    }

    Mesh MeshLoader::CreatePlane(float width, float height, IRHI* rhi) {
        float halfW = width  * 0.5f;
        float halfH = height * 0.5f;

        std::vector<Vertex> vertices = {
            { .Position = {-halfW, 0.0f, -halfH}, .Normal = {0.0f, 1.0f, 0.0f}, .TexCoord = {0.0f, 0.0f}, .Tangent = {1.0f, 0.0f, 0.0f, 1.0f} },
            { .Position = { halfW, 0.0f, -halfH}, .Normal = {0.0f, 1.0f, 0.0f}, .TexCoord = {1.0f, 0.0f}, .Tangent = {1.0f, 0.0f, 0.0f, 1.0f}},
            { .Position = { halfW, 0.0f,  halfH}, .Normal = {0.0f, 1.0f, 0.0f}, .TexCoord = {1.0f, 1.0f}, .Tangent = {1.0f, 0.0f, 0.0f, 1.0f} },
            { .Position = {-halfW, 0.0f,  halfH}, .Normal = {0.0f, 1.0f, 0.0f}, .TexCoord = {0.0f, 1.0f}, .Tangent = {1.0f, 0.0f, 0.0f, 1.0f} },
        };

        std::vector<uint32_t> indices = {0, 2, 1, 0, 3, 2};

        GenerateTangents(vertices, indices);

        AABB bounds;
        for (const auto& v : vertices) {
            bounds.min = glm::min(bounds.min, v.Position);
            bounds.max = glm::max(bounds.max, v.Position);
        }

        BufferDesc vertexBufferDesc = {
            .size       = sizeof(Vertex) * vertices.size(),
            .usage      = BufferUsage::Vertex,
            .cpuVisible = false,
        };
        BufferDesc indexBufferDesc = {
            .size       = sizeof(uint32_t) * indices.size(),
            .usage      = BufferUsage::Index,
            .cpuVisible = false,
        };

        BufferHandle vertexBuffer = rhi->CreateBuffer(vertexBufferDesc);
        rhi->UploadBufferData(vertexBuffer, vertices.data(), sizeof(Vertex) * vertices.size());

        BufferHandle indexBuffer = rhi->CreateBuffer(indexBufferDesc);
        rhi->UploadBufferData(indexBuffer, indices.data(), sizeof(uint32_t) * indices.size());

        return Mesh {
            .vertexBuffer = vertexBuffer,
            .indexBuffer  = indexBuffer,
            .vertexCount  = static_cast<uint32_t>(vertices.size()),
            .indexCount   = static_cast<uint32_t>(indices.size()),
            .bounds       = bounds,
        };
    }

void MeshLoader::GenerateTangents(std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) {
    // Initialize tangents to zero
    std::vector<glm::vec3> tangents(vertices.size(), glm::vec3(0.0f));
    std::vector<glm::vec3> bitangents(vertices.size(), glm::vec3(0.0f));

    // Compute tangent for each triangle
    for (size_t i = 0; i < indices.size(); i += 3) {
        uint32_t i0 = indices[i];
        uint32_t i1 = indices[i + 1];
        uint32_t i2 = indices[i + 2];

        glm::vec3 pos0 = vertices[i0].Position;
        glm::vec3 pos1 = vertices[i1].Position;
        glm::vec3 pos2 = vertices[i2].Position;

        glm::vec2 uv0 = vertices[i0].TexCoord;
        glm::vec2 uv1 = vertices[i1].TexCoord;
        glm::vec2 uv2 = vertices[i2].TexCoord;

        glm::vec3 edge1    = pos1 - pos0;
        glm::vec3 edge2    = pos2 - pos0;
        glm::vec2 deltaUV1 = uv1 - uv0;
        glm::vec2 deltaUV2 = uv2 - uv0;

        float denom = deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y;
        if (abs(denom) < 1e-6f) continue; // degenerate UV triangle

        float f = 1.0f / denom;

        glm::vec3 tangent = {
            f * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x),
            f * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y),
            f * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z),
        };

        tangents[i0] += tangent;
        tangents[i1] += tangent;
        tangents[i2] += tangent;
    }

    // Orthogonalize and set handedness
    for (size_t i = 0; i < vertices.size(); i++) {
        glm::vec3 N = glm::normalize(vertices[i].Normal);
        glm::vec3 T = glm::normalize(tangents[i]);

        // Gram-Schmidt orthogonalize
        T = glm::normalize(T - glm::dot(T, N) * N);

        // Compute bitangent and determine handedness
        glm::vec3 B       = glm::cross(N, T);
        float handedness  = (glm::dot(B, glm::normalize(bitangents[i])) < 0.0f) ? -1.0f : 1.0f;

        vertices[i].Tangent = glm::vec4(T, handedness);
    }
}
} // Osiris