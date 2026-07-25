//
// Created by ahtal on 25/07/2026.
//

#include "SceneLoader.h"

#include <fstream>
#include <nlohmann/json.hpp>

#include "MeshLoader.h"
#include "TextureLoader.h"
#include "core/AssetManager.h"
#include "core/Log.h"
#include "glm/gtc/type_ptr.hpp"

namespace Osiris {
    void SceneLoader::Load(const std::string &path, Scene &scene, IRHI *rhi) {
        std::ifstream file(path);
        if (!file.is_open()) {
            OSIRIS_ERROR("Failed to open scene file: {}", path);
            return;
        }

        nlohmann::json json;
        file >> json;

        std::unordered_map<std::string, Mesh> meshCache;
        std::unordered_map<std::string, TextureHandle> textureCache;
        for (auto& entityJson : json["entities"]) {
            std::string name = entityJson["name"];
            Entity e = scene.CreateEntity(name);
            if (entityJson.contains("transform")) {
                auto posJson = entityJson["transform"]["position"];
                glm::vec3 position = {
                    posJson[0].get<float>(),
                    posJson[1].get<float>(),
                    posJson[2].get<float>()
                };

                auto rotJson = entityJson["transform"]["rotation"];
                glm::vec3 rotation = {
                    rotJson[0].get<float>(),
                    rotJson[1].get<float>(),
                    rotJson[2].get<float>(),
                };

                auto scaleJson = entityJson["transform"]["scale"];
                glm::vec3 scale = {
                    scaleJson[0].get<float>(),
                    scaleJson[1].get<float>(),
                    scaleJson[2].get<float>()
                };
                TransformComponent &transform = e.GetComponent<TransformComponent>();
                transform.position = position;
                transform.rotation = rotation;
                transform.scale    = scale;
            }


            if (entityJson.contains("mesh")) {
                std::string meshPath = entityJson["mesh"];
                Mesh mesh;
                if (meshCache.contains(meshPath)) {
                    mesh = meshCache[meshPath];
                } else {
                    mesh = MeshLoader::LoadFromGLTF(AssetManager::GetPath(meshPath), rhi);
                    meshCache[meshPath] = mesh;
                }
                e.AddComponent<MeshComponent>(mesh);
            }


            if (entityJson.contains("texture")) {
                std::string texturePath = entityJson["texture"];
                TextureHandle tex;
                if (textureCache.contains(texturePath)) {
                    tex = textureCache[texturePath];
                } else {
                    tex = TextureLoader::LoadFromFile(AssetManager::GetPath(texturePath), rhi);
                    textureCache[texturePath] = tex;
                }
                e.AddComponent<MaterialComponent>(tex);
            }
        }
    }

} // Osiris