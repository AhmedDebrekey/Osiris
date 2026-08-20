//
// Created by Debreky on 25/07/2026.
//

#include "SceneLoader.h"

#include <fstream>
#include <map>
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

        // Scene::SpawnModel names every primitive of one spawn call "<name>_<index>" — strips
        // that back off so primitives from the same call round-trip as one JSON "mesh" entry.
        // Only strips a trailing "_<digits>", so a hand-authored name ending in "_2" for an
        // unrelated reason isn't silently mangled.
        std::string StripPrimitiveSuffix(const std::string& name) {
            const size_t underscore = name.find_last_of('_');
            if (underscore == std::string::npos || underscore + 1 == name.size()) return name;
            for (size_t i = underscore + 1; i < name.size(); i++) {
                if (!std::isdigit(static_cast<unsigned char>(name[i]))) return name;
            }
            return name.substr(0, underscore);
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

            glm::vec3 position(0.0f);
            glm::vec3 rotation(0.0f);
            glm::vec3 scale(1.0f);
            if (entityJson.contains("transform")) {
                auto& t = entityJson["transform"];
                position = ReadVec3(t, "position", position);
                rotation = ReadVec3(t, "rotation", rotation);
                scale    = ReadVec3(t, "scale", scale);
            }

            if (entityJson.contains("mesh")) {
                std::string meshPath = entityJson["mesh"];
                for (Entity e : scene.SpawnModel(name, meshPath, rhi)) {
                    auto& transform = e.GetComponent<TransformComponent>();
                    transform.position = position;
                    transform.rotation = rotation;
                    transform.scale    = scale;
                }
                continue;
            }

            Entity entity = scene.CreateEntity(name);
            auto& transform = entity.GetComponent<TransformComponent>();
            transform.position = position;
            transform.rotation = rotation;
            transform.scale    = scale;

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
    }

    void SceneLoader::Save(const std::string& path, Scene& scene) {
        nlohmann::json json;
        json["entities"] = nlohmann::json::array();

        struct MeshGroup { std::string name; std::string relativePath; TransformComponent transform; };
        std::map<std::string, MeshGroup> meshGroups; // keyed by groupName + "|" + relativePath, sorted for stable output

        for (Entity entity : scene.GetAllEntities()) {
            if (!entity.HasComponent<TagComponent>() || !entity.HasComponent<TransformComponent>()) continue;
            const std::string& name = entity.GetComponent<TagComponent>().name;
            const TransformComponent& transform = entity.GetComponent<TransformComponent>();

            if (entity.HasComponent<ModelSourceComponent>()) {
                const std::string& relativePath = entity.GetComponent<ModelSourceComponent>().relativePath;
                const std::string groupName = StripPrimitiveSuffix(name);
                const std::string key = groupName + "|" + relativePath;
                meshGroups.try_emplace(key, MeshGroup{ groupName, relativePath, transform });
                continue;
            }

            nlohmann::json entityJson;
            entityJson["name"]      = name;
            entityJson["transform"] = WriteTransform(transform);

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

        for (auto& [key, group] : meshGroups) {
            json["entities"].push_back({
                {"name", group.name},
                {"mesh", group.relativePath},
                {"transform", WriteTransform(group.transform)},
            });
        }

        std::ofstream file(path);
        if (!file.is_open()) {
            OSIRIS_ERROR("Failed to open scene file for writing: {}", path);
            return;
        }
        file << json.dump(2);
    }
} // Osiris
