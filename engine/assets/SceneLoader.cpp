//
// Created by Debreky on 25/07/2026.
//

#include "SceneLoader.h"

#include <fstream>
#include <unordered_map>
#include <nlohmann/json.hpp>

#include "AudioLoader.h"
#include "core/AssetManager.h"
#include "core/Log.h"

namespace Osiris {
    namespace {
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

        for (auto& entityJson : json["entities"]) {
            std::string name = entityJson.value("name", std::string());
            const TransformComponent savedTransform = ReadTransform(entityJson);

            if (entityJson.contains("mesh")) {
                std::string meshPath = entityJson["mesh"];
                Entity root = scene.SpawnModel(name, meshPath, rhi);
                if (root.IsValid()) {
                    root.GetComponent<TransformComponent>() = savedTransform;
                }
                continue;
            }

            Entity entity = scene.CreateEntity(name);
            entity.GetComponent<TransformComponent>() = savedTransform;

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

            if (entityJson.contains("collider")) {
                auto& j = entityJson["collider"];
                auto& collider = entity.AddComponent<ColliderComponent>();
                collider.halfExtents = ReadVec3(j, "halfExtents", collider.halfExtents);
            }

            if (entityJson.contains("rigidBody")) {
                const std::string motionTypeStr = entityJson["rigidBody"].value("motionType", std::string("Static"));
                auto& rigidBody = entity.AddComponent<RigidBodyComponent>();
                if (motionTypeStr == "Dynamic")        rigidBody.motionType = BodyMotionType::Dynamic;
                else if (motionTypeStr == "Kinematic") rigidBody.motionType = BodyMotionType::Kinematic;
                else                                   rigidBody.motionType = BodyMotionType::Static;
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
                entity.AddComponent<ScriptComponent>().scriptPath = entityJson["script"].value("path", std::string());
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
        }

        for (const auto& entityJson : json["entities"]) {
            if (!entityJson.contains("parent")) continue;

            const std::string childName = entityJson.value("name", std::string());
            const std::string parentName = entityJson.value("parent", std::string());
            Entity child = scene.FindEntityByName(childName);
            Entity parent = scene.FindEntityByName(parentName);
            if (!child.IsValid() || !parent.IsValid()) {
                OSIRIS_WARN("Failed to restore parent '{}' for entity '{}'", parentName, childName);
                continue;
            }

            scene.SetParent(child, parent, false);
        }
    }

    void SceneLoader::Save(const std::string& path, Scene& scene) {
        nlohmann::json json;
        json["entities"] = nlohmann::json::array();

        for (Entity entity : scene.GetAllEntities()) {
            if (!entity.HasComponent<TagComponent>() || !entity.HasComponent<TransformComponent>()) continue;
            const std::string& name = entity.GetComponent<TagComponent>().name;
            const TransformComponent& transform = entity.GetComponent<TransformComponent>();

            if (entity.HasComponent<ModelSourceComponent>()) {
                const std::string& relativePath = entity.GetComponent<ModelSourceComponent>().relativePath;
                Entity parent = scene.GetParent(entity);
                // Only skip if the parent is part of the *same* spawn (matching relativePath) —
                // otherwise a model root reparented under a different model's entity (e.g. via
                // the Scene Inspector's drag-and-drop) would silently never get its own "mesh"
                // entry and vanish on the next Load.
                if (parent.IsValid() && parent.HasComponent<ModelSourceComponent>()
                    && parent.GetComponent<ModelSourceComponent>().relativePath == relativePath) {
                    continue;
                }
                nlohmann::json entityJson = {
                    {"name", name},
                    {"mesh", relativePath},
                    {"transform", WriteTransform(transform)},
                };
                WriteParent(entityJson, scene, entity);
                json["entities"].push_back(entityJson);
                continue;
            }

            nlohmann::json entityJson;
            entityJson["name"]      = name;
            entityJson["transform"] = WriteTransform(transform);
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

            if (entity.HasComponent<ColliderComponent>()) {
                entityJson["collider"] = { {"halfExtents", WriteVec3(entity.GetComponent<ColliderComponent>().halfExtents)} };
            }

            if (entity.HasComponent<RigidBodyComponent>()) {
                static constexpr const char* motionTypeNames[] = { "Static", "Kinematic", "Dynamic" };
                auto motionType = entity.GetComponent<RigidBodyComponent>().motionType;
                entityJson["rigidBody"] = { {"motionType", motionTypeNames[static_cast<int>(motionType)]} };
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
                entityJson["script"] = { {"path", entity.GetComponent<ScriptComponent>().scriptPath} };
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

            json["entities"].push_back(entityJson);
        }

        std::ofstream file(path);
        if (!file.is_open()) {
            OSIRIS_ERROR("Failed to open scene file for writing: {}", path);
            return;
        }
        file << json.dump(2);
    }
} // Osiris
