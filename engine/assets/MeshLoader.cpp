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
        if (tangentIt != primitive.attributes.end()) {
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
} // Osiris