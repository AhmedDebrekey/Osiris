//
// Created by ahtal on 21/07/2026.
//

#include "MeshLoader.h"

#include "TextureLoader.h"
#include "AnimationLoader.h"
#include "fastgltf/core.hpp"
#include "fastgltf/types.hpp"
#include "fastgltf/tools.hpp"
#include "core/AssetManager.h"
#include "core/Log.h"
#include "fastgltf/glm_element_traits.hpp"
#include <cmath>
#include <filesystem>
#include <utility>
#include <vector>
#include <unordered_map>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace Osiris {
    namespace {
        struct CachedModel {
            std::vector<GltfNode> nodes;
            std::shared_ptr<const AnimationAsset> animation;
        };
        std::unordered_map<IRHI*, std::unordered_map<std::string, CachedModel>> s_GltfModelCaches;

        // glTF node transforms are either a raw column-major matrix or separate T/R/S components.
        glm::mat4 GetNodeLocalMatrix(const fastgltf::Node& node) {
            if (const auto* mat = std::get_if<fastgltf::Node::TransformMatrix>(&node.transform)) {
                return glm::make_mat4(mat->data());
            }

            const auto& trs = std::get<fastgltf::TRS>(node.transform);
            glm::mat4 t = glm::translate(glm::mat4(1.0f),
                glm::vec3(trs.translation[0], trs.translation[1], trs.translation[2]));
            glm::quat q(trs.rotation[3], trs.rotation[0], trs.rotation[1], trs.rotation[2]);
            glm::mat4 r = glm::mat4_cast(q);
            glm::mat4 s = glm::scale(glm::mat4(1.0f),
                glm::vec3(trs.scale[0], trs.scale[1], trs.scale[2]));
            return t * r * s;
        }

        std::size_t CollectGltfNode(const fastgltf::Asset& asset, std::size_t nodeIndex,
                                    std::optional<std::size_t> parentIndex,
                                    const std::vector<std::vector<MeshPrimitive>>& meshPrimitives,
                                    std::vector<GltfNode>& result) {
            const auto& node = asset.nodes[nodeIndex];
            const std::size_t resultIndex = result.size();

            GltfNode gltfNode;
            gltfNode.name = node.name;
            gltfNode.localTransform = GetNodeLocalMatrix(node);
            gltfNode.parentIndex = parentIndex;
            gltfNode.sourceIndex = static_cast<uint32_t>(nodeIndex);
            if (node.skinIndex.has_value()) gltfNode.skinIndex = node.skinIndex.value();
            if (node.meshIndex.has_value() && node.meshIndex.value() < meshPrimitives.size()) {
                gltfNode.primitives = meshPrimitives[node.meshIndex.value()];
            }
            result.push_back(std::move(gltfNode));

            for (std::size_t child : node.children) {
                const std::size_t childIndex = CollectGltfNode(
                    asset, child, resultIndex, meshPrimitives, result);
                result[resultIndex].childIndices.push_back(childIndex);
            }
            return resultIndex;
        }
    }

    std::vector<GltfNode> MeshLoader::LoadFromGLTF(const std::string& path, IRHI* rhi,
                                                 std::shared_ptr<const AnimationAsset>* animation) {
    if (animation) animation->reset();
    const std::string cacheKey = AssetManager::NormalizePathKey(path);
    auto& modelCache = s_GltfModelCaches[rhi];
    if (const auto cached = modelCache.find(cacheKey); cached != modelCache.end()) {
        if (animation) *animation = cached->second.animation;
        return cached->second.nodes;
    }

    std::vector<GltfNode> result;

    fastgltf::Parser parser;
    fastgltf::GltfDataBuffer data;

    if (!data.loadFromFile(path)) {
        OSIRIS_ERROR("Failed to load glTF file: {}", path);
        return result;
    }

    auto asset = parser.loadGltf(&data,
        std::filesystem::path(path).parent_path(),
        fastgltf::Options::LoadExternalBuffers | fastgltf::Options::LoadGLBBuffers);

    if (asset.error() != fastgltf::Error::None) {
        OSIRIS_ERROR("Failed to parse glTF file '{}': {} ({})",
            path,
            fastgltf::getErrorName(asset.error()),
            fastgltf::getErrorMessage(asset.error()));
        return result;
    }

    std::string animationError;
    if (!asset->animations.empty() || !asset->skins.empty()) {
        if (const auto validation = fastgltf::validate(asset.get()); validation != fastgltf::Error::None) {
            OSIRIS_ERROR("Invalid animated glTF '{}': {}", path, fastgltf::getErrorMessage(validation));
            return {};
        }
    }
    const auto animationAsset = LoadAnimationAsset(asset.get(), animationError);
    if (!animationError.empty()) {
        OSIRIS_ERROR("Cannot import animations from '{}': {}", path, animationError);
        return {};
    }

    // An image referenced as both color and material data needs a separate Vulkan view because
    // sRGB decoding is part of the image format.
    std::unordered_map<size_t, TextureHandle> colorTextureCache;
    std::unordered_map<size_t, TextureHandle> dataTextureCache;

    // Helper to load a texture by image index
    auto loadTexture = [&](size_t imageIndex, TextureFormat format) -> TextureHandle {
        auto& textureCache = format == TextureFormat::RGBA8_SRGB
                           ? colorTextureCache : dataTextureCache;
        auto it = textureCache.find(imageIndex);
        if (it != textureCache.end()) return it->second;

        auto& image = asset->images[imageIndex];
        TextureHandle handle;
        if (auto* uri = std::get_if<fastgltf::sources::URI>(&image.data)) {
            const std::string texturePath = (std::filesystem::path(path).parent_path() / uri->uri.path()).string();
            handle = TextureLoader::LoadFromFile(texturePath, rhi, format);
        } else {
            const auto bytesOf = [](const fastgltf::DataSource& source) -> std::span<const uint8_t> {
                if (const auto* array = std::get_if<fastgltf::sources::Array>(&source))
                    return {array->bytes.data(), array->bytes.size()};
                if (const auto* bytes = std::get_if<fastgltf::sources::ByteView>(&source))
                    return {reinterpret_cast<const uint8_t*>(bytes->bytes.data()), bytes->bytes.size()};
                return {};
            };
            auto bytes = bytesOf(image.data);
            if (const auto* imageView = std::get_if<fastgltf::sources::BufferView>(&image.data)) {
                const auto& view = asset->bufferViews[imageView->bufferViewIndex];
                const auto bufferBytes = bytesOf(asset->buffers[view.bufferIndex].data);
                if (view.byteOffset <= bufferBytes.size() && view.byteLength <= bufferBytes.size() - view.byteOffset)
                    bytes = bufferBytes.subspan(view.byteOffset, view.byteLength);
            }
            handle = TextureLoader::LoadFromMemory(bytes, rhi, format);
            if (!handle.IsValid()) OSIRIS_ERROR("MeshLoader: failed to load embedded image {} in '{}'", imageIndex, path);
        }
        textureCache[imageIndex] = handle;
        return handle;
    };

    // Helper to get image index from a texture index
    auto getImageIndex = [&](size_t textureIndex) -> size_t {
        return asset->textures[textureIndex].imageIndex.value_or(0);
    };

    std::vector<std::vector<MeshPrimitive>> meshPrimitives(asset->meshes.size());

    // Loop over all meshes and primitives
    for (std::size_t meshIndex = 0; meshIndex < asset->meshes.size(); ++meshIndex) {
        auto& mesh = asset->meshes[meshIndex];
        for (auto& primitive : mesh.primitives) {
            std::vector<Vertex>   vertices;
            std::vector<uint32_t> indices;

            // Extract indices
            if (primitive.indicesAccessor.has_value()) {
                auto& accessor = asset->accessors[primitive.indicesAccessor.value()];
                indices.resize(accessor.count);
                std::size_t i = 0;
                fastgltf::iterateAccessor<std::uint32_t>(asset.get(), accessor,
                    [&](std::uint32_t index) { indices[i++] = index; });
            } else {
                // No index buffer — generate sequential indices after positions are loaded
            }

            // Extract positions
            auto posIt = std::find_if(primitive.attributes.begin(), primitive.attributes.end(),
                [](const auto& attr) { return attr.first == "POSITION"; });
            if (posIt != primitive.attributes.end()) {
                auto& accessor = asset->accessors[posIt->second];
                vertices.resize(accessor.count);
                std::size_t i = 0;
                fastgltf::iterateAccessor<glm::vec3>(asset.get(), accessor,
                    [&](glm::vec3 pos) { vertices[i++].Position = pos; });
            }

            // Generate sequential indices if none exist
            if (indices.empty() && !vertices.empty()) {
                indices.resize(vertices.size());
                for (uint32_t i = 0; i < vertices.size(); i++) indices[i] = i;
            }

            // Extract normals
            auto normIt = std::find_if(primitive.attributes.begin(), primitive.attributes.end(),
                [](const auto& attr) { return attr.first == "NORMAL"; });
            if (normIt != primitive.attributes.end()) {
                auto& accessor = asset->accessors[normIt->second];
                std::size_t i = 0;
                fastgltf::iterateAccessor<glm::vec3>(asset.get(), accessor,
                    [&](glm::vec3 norm) { vertices[i++].Normal = norm; });
            }

            // Extract UVs
            auto uvIt = std::find_if(primitive.attributes.begin(), primitive.attributes.end(),
                [](const auto& attr) { return attr.first == "TEXCOORD_0"; });
            if (uvIt != primitive.attributes.end()) {
                auto& accessor = asset->accessors[uvIt->second];
                std::size_t i = 0;
                fastgltf::iterateAccessor<glm::vec2>(asset.get(), accessor,
                    [&](glm::vec2 uv) { vertices[i++].TexCoord = uv; });
            }

            // Extract tangents or generate them
            auto tangentIt = std::find_if(primitive.attributes.begin(), primitive.attributes.end(),
                [](const auto& attr) { return attr.first == "TANGENT"; });
            if (tangentIt != primitive.attributes.end()) {
                auto& accessor = asset->accessors[tangentIt->second];
                std::size_t i = 0;
                fastgltf::iterateAccessor<glm::vec4>(asset.get(), accessor,
                    [&](glm::vec4 tangent) { vertices[i++].Tangent = tangent; });
            } else {
                GenerateTangents(vertices, indices);
            }

            if (vertices.empty() || indices.empty()) {
                OSIRIS_ERROR("MeshLoader: empty primitive skipped");
                continue;
            }

            std::vector<SkinVertex> skinVertices;
            std::shared_ptr<std::vector<AABB>> jointBounds;
            const auto jointsIt = primitive.findAttribute("JOINTS_0");
            const auto weightsIt = primitive.findAttribute("WEIGHTS_0");
            if (jointsIt != primitive.attributes.end() || weightsIt != primitive.attributes.end()) {
                if (jointsIt == primitive.attributes.end() || weightsIt == primitive.attributes.end()
                    || primitive.findAttribute("JOINTS_1") != primitive.attributes.end()
                    || primitive.findAttribute("WEIGHTS_1") != primitive.attributes.end()) {
                    OSIRIS_ERROR("MeshLoader: '{}' requires exactly one JOINTS_0/WEIGHTS_0 pair (four influences)", path);
                    return {};
                }
                const auto& jointsAccessor = asset->accessors[jointsIt->second];
                const auto& weightsAccessor = asset->accessors[weightsIt->second];
                if (jointsAccessor.count != vertices.size() || weightsAccessor.count != vertices.size()
                    || jointsAccessor.type != fastgltf::AccessorType::Vec4 || weightsAccessor.type != fastgltf::AccessorType::Vec4) {
                    OSIRIS_ERROR("MeshLoader: skin vertex attribute count/type mismatch in '{}'", path);
                    return {};
                }
                skinVertices.resize(vertices.size());
                size_t index = 0;
                fastgltf::iterateAccessor<glm::uvec4>(asset.get(), jointsAccessor,
                    [&](glm::uvec4 value) { skinVertices[index++].joints = value; });
                index = 0;
                fastgltf::iterateAccessor<glm::vec4>(asset.get(), weightsAccessor,
                    [&](glm::vec4 value) { skinVertices[index++].weights = value; });
                size_t jointCount = 0;
                for (const auto& node : asset->nodes) {
                    if (node.meshIndex.has_value() && node.meshIndex.value() == meshIndex && node.skinIndex.has_value()) {
                        const size_t count = asset->skins[node.skinIndex.value()].joints.size();
                        jointCount = jointCount == 0 ? count : std::min(jointCount, count);
                    }
                }
                if (jointCount == 0) { OSIRIS_ERROR("MeshLoader: skin attributes have no skin in '{}'", path); return {}; }
                jointBounds = std::make_shared<std::vector<AABB>>(jointCount);
                for (size_t vertex = 0; vertex < skinVertices.size(); vertex++) {
                    auto& skin = skinVertices[vertex];
                    for (int axis = 0; axis < 4; axis++) {
                        if (!std::isfinite(skin.weights[axis]) || skin.weights[axis] < 0.0f) {
                            OSIRIS_ERROR("MeshLoader: invalid skin weight in '{}'", path); return {};
                        }
                        if (skin.weights[axis] == 0.0f) skin.joints[axis] = 0;
                        if (skin.joints[axis] >= jointCount) {
                            OSIRIS_ERROR("MeshLoader: skin joint index is out of range in '{}'", path); return {};
                        }
                    }
                    const float sum = glm::dot(skin.weights, glm::vec4(1.0f));
                    if (!std::isfinite(sum) || sum <= 0.0f) {
                        OSIRIS_ERROR("MeshLoader: vertex has no valid skin weights in '{}'", path); return {};
                    }
                    skin.weights /= sum;
                    for (int axis = 0; axis < 4; axis++) if (skin.weights[axis] > 0.0f) {
                        auto& bounds = (*jointBounds)[skin.joints[axis]];
                        bounds.min = glm::min(bounds.min, vertices[vertex].Position);
                        bounds.max = glm::max(bounds.max, vertices[vertex].Position);
                    }
                }
            }

            // Compute AABB
            AABB bounds;
            for (const auto& v : vertices) {
                bounds.min = glm::min(bounds.min, v.Position);
                bounds.max = glm::max(bounds.max, v.Position);
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

            Mesh mesh = {
                .vertexBuffer = vertexBuffer,
                .indexBuffer  = indexBuffer,
                .vertexCount  = static_cast<uint32_t>(vertices.size()),
                .indexCount   = static_cast<uint32_t>(indices.size()),
                .bounds       = bounds,
            };
            if (!skinVertices.empty()) {
                mesh.skinVertexBuffer = rhi->CreateBuffer({
                    .size = skinVertices.size() * sizeof(SkinVertex), .usage = BufferUsage::Vertex});
                rhi->UploadBufferData(mesh.skinVertexBuffer, skinVertices.data(), skinVertices.size() * sizeof(SkinVertex));
                mesh.jointBounds = std::move(jointBounds);
            }

            // Load material
            MaterialDesc matDesc;

            if (primitive.materialIndex.has_value()) {
                auto& mat = asset->materials[primitive.materialIndex.value()];

                for (size_t channel = 0; channel < mat.pbrData.baseColorFactor.size(); channel++) {
                    matDesc.baseColorFactor[channel] = static_cast<float>(
                        mat.pbrData.baseColorFactor[channel]);
                }
                switch (mat.alphaMode) {
                case fastgltf::AlphaMode::Mask:
                    matDesc.alphaMode = MaterialAlphaMode::Mask;
                    break;
                case fastgltf::AlphaMode::Blend:
                    matDesc.alphaMode = MaterialAlphaMode::Blend;
                    break;
                default:
                    matDesc.alphaMode = MaterialAlphaMode::Opaque;
                    break;
                }
                matDesc.alphaCutoff = static_cast<float>(mat.alphaCutoff);
                matDesc.doubleSided = mat.doubleSided;
                matDesc.metallicFactor = static_cast<float>(mat.pbrData.metallicFactor);
                matDesc.roughnessFactor = static_cast<float>(mat.pbrData.roughnessFactor);

                // Albedo
                if (mat.pbrData.baseColorTexture.has_value()) {
                    size_t texIndex = mat.pbrData.baseColorTexture->textureIndex;
                    matDesc.albedo = loadTexture(getImageIndex(texIndex), TextureFormat::RGBA8_SRGB);
                }

                // Metallic + Roughness (combined texture)
                if (mat.pbrData.metallicRoughnessTexture.has_value()) {
                    size_t texIndex = mat.pbrData.metallicRoughnessTexture->textureIndex;
                    TextureHandle handle = loadTexture(getImageIndex(texIndex), TextureFormat::RGBA8_UNORM);
                    matDesc.metallic  = handle;
                    matDesc.roughness = handle;
                }

                // Normal
                if (mat.normalTexture.has_value()) {
                    matDesc.normalScale = static_cast<float>(mat.normalTexture->scale);
                    size_t texIndex = mat.normalTexture->textureIndex;
                    matDesc.normal = loadTexture(getImageIndex(texIndex), TextureFormat::RGBA8_UNORM);
                }

                // AO
                if (mat.occlusionTexture.has_value()) {
                    size_t texIndex = mat.occlusionTexture->textureIndex;
                    matDesc.ao = loadTexture(getImageIndex(texIndex), TextureFormat::RGBA8_UNORM);
                }
            }

            MaterialHandle material = rhi->CreateMaterial(matDesc);

            meshPrimitives[meshIndex].push_back({ mesh, material });
        }
    }

    if (asset->scenes.empty()) {
        OSIRIS_ERROR("MeshLoader: glTF has no scenes: {}", path);
        return result;
    }

    const std::size_t sceneIndex = asset->defaultScene.value_or(0);
    for (std::size_t nodeIndex : asset->scenes[sceneIndex].nodeIndices) {
        CollectGltfNode(asset.get(), nodeIndex, std::nullopt, meshPrimitives, result);
    }

    if (animationAsset) {
        std::vector<bool> included(asset->nodes.size(), false);
        for (const auto& node : result) included[node.sourceIndex] = true;
        for (const auto& node : result) if (node.skinIndex.has_value()) {
            for (uint32_t joint : animationAsset->skins[*node.skinIndex].joints) if (!included[joint]) {
                OSIRIS_ERROR("MeshLoader: skin joint is outside the default scene in '{}'", path);
                return {};
            }
            for (const auto& primitive : node.primitives) if (!primitive.mesh.skinVertexBuffer.IsValid()) {
                OSIRIS_ERROR("MeshLoader: skinned node is missing joint/weight vertex data in '{}'", path);
                return {};
            }
        }
    }

    std::size_t primitiveCount = 0;
    for (const GltfNode& node : result) primitiveCount += node.primitives.size();
    OSIRIS_INFO("MeshLoader: loaded {} nodes and {} primitives from {}", result.size(), primitiveCount, path);
    if (!result.empty()) {
        modelCache.emplace(cacheKey, CachedModel{result, animationAsset});
    }
    if (animation) *animation = animationAsset;
    return result;
}

    void MeshLoader::ClearCache(IRHI* rhi) {
        s_GltfModelCaches.erase(rhi);
    }

    std::vector<GltfPrimitiveInstance> MeshLoader::FlattenPrimitives(const std::vector<GltfNode>& nodes) {
        std::vector<GltfPrimitiveInstance> result;

        auto collectNode = [&](auto&& self, std::size_t nodeIndex, const glm::mat4& parentTransform) -> void {
            const GltfNode& node = nodes[nodeIndex];
            const glm::mat4 transform = parentTransform * node.localTransform;
            for (const MeshPrimitive& primitive : node.primitives) {
                result.push_back({primitive, transform});
            }
            for (std::size_t childIndex : node.childIndices) {
                self(self, childIndex, transform);
            }
        };

        for (std::size_t i = 0; i < nodes.size(); i++) {
            if (!nodes[i].parentIndex.has_value()) {
                collectNode(collectNode, i, glm::mat4(1.0f));
            }
        }
        return result;
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

    Mesh MeshLoader::CreateBox(const glm::vec3& halfExtents, IRHI* rhi) {
        const glm::vec3& h = halfExtents;

        // 4 verts per face (not a shared 8-vert cube) so each face gets its own normal/UV,
        // same reasoning CreatePlane already follows for a single quad. right/up are chosen
        // so that up x right == normal and the {0,2,1, 0,3,2} winding below matches
        // CreatePlane's front-face convention exactly (CreatePlane: R=+X, U=+Z, N=+Y, and
        // indeed Z x X = Y) — reusing a different winding here would cull half the box.
        struct Face { glm::vec3 normal; glm::vec3 right; glm::vec3 up; };
        const Face faces[6] = {
            { { 1, 0, 0}, {0, 0, 1}, {0, 1, 0} }, // +X: Y x Z = X
            { {-1, 0, 0}, {0, 0,-1}, {0, 1, 0} }, // -X: Y x -Z = -X
            { { 0, 1, 0}, {1, 0, 0}, {0, 0, 1} }, // +Y: Z x X = Y
            { { 0,-1, 0}, {-1,0, 0}, {0, 0, 1} }, // -Y: Z x -X = -Y
            { { 0, 0, 1}, {0, 1, 0}, {1, 0, 0} }, // +Z: X x Y = Z
            { { 0, 0,-1}, {0,-1, 0}, {1, 0, 0} }, // -Z: X x -Y = -Z
        };

        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        vertices.reserve(24);
        indices.reserve(36);

        for (const Face& face : faces) {
            glm::vec3 center = face.normal * h;
            glm::vec3 right  = face.right * h;
            glm::vec3 up     = face.up * h;

            uint32_t base = static_cast<uint32_t>(vertices.size());
            vertices.push_back({ .Position = center - right - up, .Normal = face.normal, .TexCoord = {0.0f, 0.0f}, .Tangent = glm::vec4(glm::normalize(face.right), 1.0f) });
            vertices.push_back({ .Position = center + right - up, .Normal = face.normal, .TexCoord = {1.0f, 0.0f}, .Tangent = glm::vec4(glm::normalize(face.right), 1.0f) });
            vertices.push_back({ .Position = center + right + up, .Normal = face.normal, .TexCoord = {1.0f, 1.0f}, .Tangent = glm::vec4(glm::normalize(face.right), 1.0f) });
            vertices.push_back({ .Position = center - right + up, .Normal = face.normal, .TexCoord = {0.0f, 1.0f}, .Tangent = glm::vec4(glm::normalize(face.right), 1.0f) });

            indices.insert(indices.end(), { base, base + 2, base + 1, base, base + 3, base + 2 });
        }

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
        glm::vec3 bitangent = {
            f * (-deltaUV2.x * edge1.x + deltaUV1.x * edge2.x),
            f * (-deltaUV2.x * edge1.y + deltaUV1.x * edge2.y),
            f * (-deltaUV2.x * edge1.z + deltaUV1.x * edge2.z),
        };

        tangents[i0] += tangent;
        tangents[i1] += tangent;
        tangents[i2] += tangent;
        bitangents[i0] += bitangent;
        bitangents[i1] += bitangent;
        bitangents[i2] += bitangent;
    }

    // Orthogonalize and set handedness
    for (size_t i = 0; i < vertices.size(); i++) {
        glm::vec3 N = glm::normalize(vertices[i].Normal);
        glm::vec3 T = tangents[i] - glm::dot(tangents[i], N) * N;
        if (glm::dot(T, T) < 1e-12f) {
            const glm::vec3 reference = std::abs(N.z) < 0.999f
                ? glm::vec3(0.0f, 0.0f, 1.0f)
                : glm::vec3(0.0f, 1.0f, 0.0f);
            T = glm::cross(reference, N);
        }
        T = glm::normalize(T);

        const glm::vec3 B = glm::cross(N, T);
        const float handedness = glm::dot(bitangents[i], bitangents[i]) >= 1e-12f &&
                                 glm::dot(B, bitangents[i]) < 0.0f
                               ? -1.0f : 1.0f;

        vertices[i].Tangent = glm::vec4(T, handedness);
    }
}
} // Osiris
