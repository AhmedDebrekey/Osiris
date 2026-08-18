//
// Created by Debreky on 25/07/2026.
//

#include "SceneLoader.h"

#include <fstream>
#include <nlohmann/json.hpp>

#include "MeshLoader.h"
#include "TextureLoader.h"
#include "core/AssetManager.h"
#include "core/Log.h"

namespace Osiris {

void SceneLoader::Load(const std::string& path, Scene& scene, IRHI* rhi) {
    std::ifstream file(path);
    if (!file.is_open()) {
        OSIRIS_ERROR("Failed to open scene file: {}", path);
        return;
    }

    nlohmann::json json;
    file >> json;

    // Caches
    std::unordered_map<std::string, std::vector<MeshPrimitive>> meshCache;
    std::unordered_map<std::string, TextureHandle>              textureCache;

    for (auto& entityJson : json["entities"]) {
        std::string name = entityJson["name"];

        // If entity has a mesh — create one entity per primitive
        if (entityJson.contains("mesh")) {
            std::string meshPath = entityJson["mesh"];

            // Load or retrieve from cache
            if (!meshCache.contains(meshPath)) {
                meshCache[meshPath] = MeshLoader::LoadFromGLTF(
                    AssetManager::GetPath(meshPath), rhi);
            }

            auto& primitives = meshCache[meshPath];

            // Read transform once — shared across all primitives
            glm::vec3 position = glm::vec3(0.0f);
            glm::vec3 rotation = glm::vec3(0.0f);
            glm::vec3 scale    = glm::vec3(1.0f);

            if (entityJson.contains("transform")) {
                auto posJson = entityJson["transform"]["position"];
                position = { posJson[0], posJson[1], posJson[2] };

                auto rotJson = entityJson["transform"]["rotation"];
                rotation = { rotJson[0], rotJson[1], rotJson[2] };

                auto scaleJson = entityJson["transform"]["scale"];
                scale = { scaleJson[0], scaleJson[1], scaleJson[2] };
            }

            for (uint32_t i = 0; i < primitives.size(); i++) {
                std::string entityName = name + "_" + std::to_string(i);
                Entity e = scene.CreateEntity(entityName);

                auto& transform    = e.GetComponent<TransformComponent>();
                transform.position = position;
                transform.rotation = rotation;
                transform.scale    = scale;

                e.AddComponent<MeshComponent>(primitives[i].mesh);

                // Use material from glTF if available
                // Override with explicit texture if specified in JSON
                MaterialHandle mat = primitives[i].material;

                if (entityJson.contains("texture")) {
                    std::string texturePath = entityJson["texture"];
                    TextureHandle tex;
                    if (textureCache.contains(texturePath)) {
                        tex = textureCache[texturePath];
                    } else {
                        tex = TextureLoader::LoadFromFile(
                            AssetManager::GetPath(texturePath), rhi);
                        textureCache[texturePath] = tex;
                    }
                    mat = rhi->CreateMaterial({ .albedo = tex });
                }

                e.AddComponent<MaterialComponent>(mat);
            }
        } else {
            // No mesh — just create entity with transform
            Entity e = scene.CreateEntity(name);

            if (entityJson.contains("transform")) {
                auto posJson = entityJson["transform"]["position"];
                auto rotJson = entityJson["transform"]["rotation"];
                auto scaleJson = entityJson["transform"]["scale"];

                auto& transform    = e.GetComponent<TransformComponent>();
                transform.position = { posJson[0], posJson[1], posJson[2] };
                transform.rotation = { rotJson[0], rotJson[1], rotJson[2] };
                transform.scale    = { scaleJson[0], scaleJson[1], scaleJson[2] };
            }
        }
    }
}

} // Osiris