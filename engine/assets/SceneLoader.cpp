//
// Created by Debreky on 25/07/2026.
//

#include "SceneLoader.h"

#include <array>
#include <fstream>
#include <functional>
#include <unordered_map>
#include <nlohmann/json.hpp>

#include "AudioLoader.h"
#include "MeshLoader.h"
#include "TextureLoader.h"
#include "core/AssetManager.h"
#include "core/Log.h"

namespace Osiris {
    namespace {
        using BoxMeshKey = std::array<float, 3>;

        struct BoxMeshKeyHash {
            size_t operator()(const BoxMeshKey& key) const noexcept {
                size_t hash = std::hash<float>{}(key[0]);
                hash ^= std::hash<float>{}(key[1]) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
                hash ^= std::hash<float>{}(key[2]) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
                return hash;
            }
        };

        glm::vec3 ReadVec3(const nlohmann::json& parent, const char* key, const glm::vec3& fallback) {
            if (!parent.contains(key)) return fallback;
            const auto& j = parent[key];
            if (!j.is_array() || j.size() != 3) return fallback;
            return { j[0].get<float>(), j[1].get<float>(), j[2].get<float>() };
        }

        nlohmann::json WriteVec3(const glm::vec3& v) {
            return nlohmann::json::array({ v.x, v.y, v.z });
        }

        nlohmann::json WriteTransform(const TransformComponent& t) {
            return {
                {"position", WriteVec3(t.position)},
                {"rotation", WriteVec3(t.rotation)},
                {"scale",    WriteVec3(t.scale)},
            };
        }

        TransformComponent ReadTransform(const nlohmann::json& entityJson) {
            TransformComponent transform;
            if (!entityJson.contains("transform")) return transform;

            const auto& transformJson = entityJson["transform"];
            transform.position = ReadVec3(transformJson, "position", transform.position);
            transform.rotation = ReadVec3(transformJson, "rotation", transform.rotation);
            transform.scale = ReadVec3(transformJson, "scale", transform.scale);
            return transform;
        }

        void WriteParent(nlohmann::json& entityJson, Scene& scene, Entity entity) {
            Entity parent = scene.GetParent(entity);
            if (parent.IsValid() && parent.HasComponent<TagComponent>()) {
                entityJson["parent"] = parent.GetComponent<TagComponent>().name;
            }
        }

    }

    void SceneLoader::Load(const std::string& path, Scene& scene, IRHI* rhi, IAudio* audio) {
        std::ifstream file(path);
        if (!file.is_open()) {
            OSIRIS_ERROR("Failed to open scene file: {}", path);
            return;
        }

        nlohmann::json json;
        file >> json;

        std::unordered_map<std::string, AudioBufferHandle> audioCache;
        std::unordered_map<BoxMeshKey, Mesh, BoxMeshKeyHash> boxMeshCache;
        std::unordered_map<std::string, MaterialHandle> boxMaterialCache;

        // Spawn every model first so lightweight model-child override entries can resolve their
        // generated entities regardless of the order EnTT wrote them into the scene file. Kept by
        // index rather than re-found by name below: entity names aren't guaranteed unique (two
        // "mesh" entries can share a name, e.g. two placed copies of the same prop), and a
        // name-based re-lookup would silently collapse both onto whichever entity matched first.
        std::vector<Entity> spawnedRoots(json["entities"].size());
        for (std::size_t i = 0; i < json["entities"].size(); i++) {
            const auto& entityJson = json["entities"][i];
            if (!entityJson.contains("mesh")) continue;

            const std::string name = entityJson.value("name", std::string());
            const std::string meshPath = entityJson.value("mesh", std::string());
            spawnedRoots[i] = scene.SpawnModel(name, meshPath, rhi);
        }

        // modelChild overrides and the parent-restoration pass below still resolve by name (they
        // target glTF-generated child nodes and later plain entities, neither of which has a
        // stable index into spawnedRoots), so maintain one name->Entity map instead of having
        // Scene::FindEntityByName rescan the whole registry for every lookup.
        std::unordered_map<std::string, Entity> entitiesByName;
        for (Entity candidate : scene.GetAllEntities()) {
            if (candidate.HasComponent<TagComponent>()) {
                entitiesByName[candidate.GetComponent<TagComponent>().name] = candidate;
            }
        }

        for (std::size_t i = 0; i < json["entities"].size(); i++) {
            auto& entityJson = json["entities"][i];
            std::string name = entityJson.value("name", std::string());
            const TransformComponent savedTransform = ReadTransform(entityJson);

            Entity entity;
            const bool isModelChildEntry = entityJson.contains("modelChild");
            const bool isModelEntry = entityJson.contains("mesh") || isModelChildEntry;
            if (entityJson.contains("mesh")) {
                entity = spawnedRoots[i];
            } else if (isModelChildEntry) {
                if (!entityJson["modelChild"].is_string()) {
                    OSIRIS_WARN("Model child override '{}' has an invalid model source", name);
                    continue;
                }

                const std::string modelSource = entityJson["modelChild"].get<std::string>();
                const auto found = entitiesByName.find(name);
                entity = found != entitiesByName.end() ? found->second : Entity{};
                if (!entity.IsValid() || !entity.HasComponent<ModelSourceComponent>()
                    || entity.GetComponent<ModelSourceComponent>().relativePath != modelSource) {
                    OSIRIS_WARN("Failed to resolve model child override '{}' from '{}'", name, modelSource);
                    continue;
                }
            } else {
                entity = scene.CreateEntity(name);
                entitiesByName[name] = entity;
            }

            if (!entity.IsValid()) continue;
            entity.GetComponent<TransformComponent>() = savedTransform;

            if (entityJson.contains("box") && !isModelEntry) {
                auto& j = entityJson["box"];
                const glm::vec3 halfExtents = ReadVec3(j, "halfExtents", ColliderComponent{}.halfExtents);
                const std::string texturePath = j.value("texture", std::string());
                const BoxMeshKey meshKey = { halfExtents.x, halfExtents.y, halfExtents.z };

                auto cachedMesh = boxMeshCache.find(meshKey);
                if (cachedMesh == boxMeshCache.end()) {
                    cachedMesh = boxMeshCache.emplace(meshKey, MeshLoader::CreateBox(halfExtents, rhi)).first;
                }
                entity.AddComponent<MeshComponent>(cachedMesh->second);

                auto cachedMaterial = boxMaterialCache.find(texturePath);
                if (cachedMaterial == boxMaterialCache.end()) {
                    TextureHandle texture;
                    if (!texturePath.empty()) {
                        texture = TextureLoader::LoadFromFile(AssetManager::GetPath(texturePath), rhi);
                    }
                    MaterialHandle material = rhi->CreateMaterial({.albedo = texture});
                    cachedMaterial = boxMaterialCache.emplace(texturePath, material).first;
                }
                entity.AddComponent<MaterialComponent>(cachedMaterial->second);

                auto& boxSource = entity.AddComponent<BoxSourceComponent>();
                boxSource.halfExtents = halfExtents;
                boxSource.texturePath = texturePath;
            }

            if (entityJson.contains("spotLight")) {
                auto& j = entityJson["spotLight"];
                auto& light = entity.AddComponent<SpotLightComponent>();
                light.color       = ReadVec3(j, "color", light.color);
                light.intensity   = j.value("intensity", light.intensity);
                light.innerCone   = j.value("innerCone", light.innerCone);
                light.outerCone   = j.value("outerCone", light.outerCone);
                light.range       = j.value("range", light.range);
                light.enabled     = j.value("enabled", light.enabled);
                light.castsShadow = j.value("castsShadow", light.castsShadow);
            }

            if (entityJson.contains("emissive")) {
                auto& j = entityJson["emissive"];
                auto& emissive = entity.AddComponent<EmissiveComponent>();
                emissive.color = ReadVec3(j, "color", emissive.color);
                emissive.intensity = j.value("intensity", emissive.intensity);
            }

            if (entityJson.contains("collider")) {
                auto& j = entityJson["collider"];
                auto& collider = entity.AddComponent<ColliderComponent>();
                collider.halfExtents = ReadVec3(j, "halfExtents", collider.halfExtents);
                collider.center = ReadVec3(j, "center", collider.center);
            }

            if (entityJson.contains("rigidBody")) {
                auto& j = entityJson["rigidBody"];
                const std::string motionTypeStr = j.value("motionType", std::string("Static"));
                auto& rigidBody = entity.AddComponent<RigidBodyComponent>();
                if (motionTypeStr == "Dynamic")        rigidBody.motionType = BodyMotionType::Dynamic;
                else if (motionTypeStr == "Kinematic") rigidBody.motionType = BodyMotionType::Kinematic;
                else                                   rigidBody.motionType = BodyMotionType::Static;
                rigidBody.lockRotationToYAxis = j.value("lockRotationToYAxis", rigidBody.lockRotationToYAxis);
                rigidBody.isSensor = j.value("isSensor", rigidBody.isSensor);
            }

            if (entityJson.contains("audioSource")) {
                auto& j = entityJson["audioSource"];
                auto& audioSrc = entity.AddComponent<AudioSourceComponent>();
                audioSrc.clipPath          = j.value("clipPath", std::string());
                audioSrc.gain              = j.value("gain", audioSrc.gain);
                audioSrc.pitch             = j.value("pitch", audioSrc.pitch);
                audioSrc.loop              = j.value("loop", audioSrc.loop);
                audioSrc.autoPlay          = j.value("autoPlay", audioSrc.autoPlay);
                audioSrc.referenceDistance = j.value("referenceDistance", audioSrc.referenceDistance);
                audioSrc.maxDistance       = j.value("maxDistance", audioSrc.maxDistance);
                audioSrc.rolloffFactor     = j.value("rolloffFactor", audioSrc.rolloffFactor);

                if (!audioSrc.clipPath.empty()) {
                    auto cached = audioCache.find(audioSrc.clipPath);
                    if (cached == audioCache.end()) {
                        PCMAudioData pcm = AudioLoader::LoadWAV(AssetManager::GetPath(audioSrc.clipPath));
                        AudioBufferHandle handle = pcm.pcmData.empty() ? AudioBufferHandle{} : audio->CreateBuffer(pcm);
                        cached = audioCache.emplace(audioSrc.clipPath, handle).first;
                    }
                    audioSrc.clip = cached->second;
                }
            }

            if (entityJson.contains("script")) {
                entity.AddComponent<ScriptComponent>().scriptPath = AssetManager::GetRelativePath(
                    entityJson["script"].value("path", std::string()));
            }

            if (entityJson.contains("camera")) {
                auto& j = entityJson["camera"];
                auto& cam = entity.AddComponent<CameraComponent>();
                cam.eyeHeight = j.value("eyeHeight", cam.eyeHeight);
                cam.isPrimary = j.value("isPrimary", cam.isPrimary);
            }

            if (entityJson.contains("character")) {
                auto& j = entityJson["character"];
                auto& character = entity.AddComponent<CharacterComponent>();
                character.radius           = j.value("radius", character.radius);
                character.height           = j.value("height", character.height);
                character.maxSlopeAngleDeg = j.value("maxSlopeAngleDeg", character.maxSlopeAngleDeg);
                character.mass             = j.value("mass", character.mass);
            }

            if (entityJson.contains("interactable")) {
                auto& j = entityJson["interactable"];
                auto& interactable = entity.AddComponent<InteractableComponent>();
                interactable.prompt = j.value("prompt", interactable.prompt);
                interactable.maxDistance = j.value("maxDistance", interactable.maxDistance);
                const int keyCode = glm::clamp(j.value("keyCode", static_cast<int>(interactable.keyCode)),
                    0, SDL_NUM_SCANCODES - 1);
                interactable.keyCode = static_cast<SDL_Scancode>(keyCode);
            }
        }

        for (const auto& entityJson : json["entities"]) {
            if (!entityJson.contains("parent")) continue;

            const std::string childName = entityJson.value("name", std::string());
            const std::string parentName = entityJson.value("parent", std::string());
            const auto childIt = entitiesByName.find(childName);
            const auto parentIt = entitiesByName.find(parentName);
            Entity child = childIt != entitiesByName.end() ? childIt->second : Entity{};
            Entity parent = parentIt != entitiesByName.end() ? parentIt->second : Entity{};
            if (!child.IsValid() || !parent.IsValid()) {
                OSIRIS_WARN("Failed to restore parent '{}' for entity '{}'", parentName, childName);
                continue;
            }

            scene.SetParent(child, parent, false);
        }
    }

    bool SceneLoader::Save(const std::string& path, Scene& scene) {
        nlohmann::json json;
        json["entities"] = nlohmann::json::array();

        for (Entity entity : scene.GetAllEntities()) {
            if (!entity.HasComponent<TagComponent>() || !entity.HasComponent<TransformComponent>()) continue;
            const std::string& name = entity.GetComponent<TagComponent>().name;
            const TransformComponent& transform = entity.GetComponent<TransformComponent>();

            nlohmann::json entityJson = {
                {"name", name},
                {"transform", WriteTransform(transform)},
            };

            if (entity.HasComponent<ModelSourceComponent>()) {
                const std::string& relativePath = entity.GetComponent<ModelSourceComponent>().relativePath;
                Entity parent = scene.GetParent(entity);
                // Only skip if the parent is part of the *same* spawn (matching relativePath) —
                // otherwise a model root reparented under a different model's entity (e.g. via
                // the Scene Inspector's drag-and-drop) would silently never get its own "mesh"
                // entry and vanish on the next Load.
                if (parent.IsValid() && parent.HasComponent<ModelSourceComponent>()
                    && parent.GetComponent<ModelSourceComponent>().relativePath == relativePath) {
                    entityJson["modelChild"] = relativePath;
                } else {
                    entityJson["mesh"] = relativePath;
                }
            } else if (entity.HasComponent<BoxSourceComponent>()) {
                const auto& boxSource = entity.GetComponent<BoxSourceComponent>();
                entityJson["box"] = {
                    {"halfExtents", WriteVec3(boxSource.halfExtents)},
                    {"texture", boxSource.texturePath},
                };
            }

            WriteParent(entityJson, scene, entity);

            if (entity.HasComponent<SpotLightComponent>()) {
                auto& light = entity.GetComponent<SpotLightComponent>();
                entityJson["spotLight"] = {
                    {"color", WriteVec3(light.color)},
                    {"intensity", light.intensity},
                    {"innerCone", light.innerCone},
                    {"outerCone", light.outerCone},
                    {"range", light.range},
                    {"enabled", light.enabled},
                    {"castsShadow", light.castsShadow},
                };
            }

            if (entity.HasComponent<EmissiveComponent>()) {
                auto& emissive = entity.GetComponent<EmissiveComponent>();
                entityJson["emissive"] = {
                    {"color", WriteVec3(emissive.color)},
                    {"intensity", emissive.intensity},
                };
            }

            if (entity.HasComponent<ColliderComponent>()) {
                const auto& collider = entity.GetComponent<ColliderComponent>();
                entityJson["collider"] = {
                    {"halfExtents", WriteVec3(collider.halfExtents)},
                    {"center", WriteVec3(collider.center)},
                };
            }

            if (entity.HasComponent<RigidBodyComponent>()) {
                static constexpr const char* motionTypeNames[] = { "Static", "Kinematic", "Dynamic" };
                auto& rigidBody = entity.GetComponent<RigidBodyComponent>();
                entityJson["rigidBody"] = {
                    {"motionType", motionTypeNames[static_cast<int>(rigidBody.motionType)]},
                    {"lockRotationToYAxis", rigidBody.lockRotationToYAxis},
                    {"isSensor", rigidBody.isSensor},
                };
            }

            if (entity.HasComponent<AudioSourceComponent>()) {
                auto& src = entity.GetComponent<AudioSourceComponent>();
                entityJson["audioSource"] = {
                    {"clipPath", src.clipPath},
                    {"gain", src.gain},
                    {"pitch", src.pitch},
                    {"loop", src.loop},
                    {"autoPlay", src.autoPlay},
                    {"referenceDistance", src.referenceDistance},
                    {"maxDistance", src.maxDistance},
                    {"rolloffFactor", src.rolloffFactor},
                };
            }

            if (entity.HasComponent<ScriptComponent>()) {
                entityJson["script"] = { {"path", AssetManager::GetRelativePath(
                    entity.GetComponent<ScriptComponent>().scriptPath)} };
            }

            if (entity.HasComponent<CameraComponent>()) {
                auto& cam = entity.GetComponent<CameraComponent>();
                entityJson["camera"] = { {"eyeHeight", cam.eyeHeight}, {"isPrimary", cam.isPrimary} };
            }

            if (entity.HasComponent<CharacterComponent>()) {
                auto& character = entity.GetComponent<CharacterComponent>();
                entityJson["character"] = {
                    {"radius", character.radius},
                    {"height", character.height},
                    {"maxSlopeAngleDeg", character.maxSlopeAngleDeg},
                    {"mass", character.mass},
                };
            }

            if (entity.HasComponent<InteractableComponent>()) {
                const auto& interactable = entity.GetComponent<InteractableComponent>();
                entityJson["interactable"] = {
                    {"prompt", interactable.prompt},
                    {"maxDistance", interactable.maxDistance},
                    {"keyCode", static_cast<int>(interactable.keyCode)},
                };
            }

            json["entities"].push_back(entityJson);
        }

        std::ofstream file(path);
        if (!file.is_open()) {
            OSIRIS_ERROR("Failed to open scene file for writing: {}", path);
            return false;
        }
        file << json.dump(2);
        return file.good();
    }
} // Osiris
