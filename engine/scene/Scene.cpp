//
// Created by ahtal on 25/07/2026.
//

#include "Scene.h"

#include <algorithm>

#include "renderer/Frustum.h"
#include "renderer/Camera.h"
#include "rhi/RHI.h"
#include "physics/IPhysics.h"
#include "audio/IAudio.h"
#include "scripting/IScripting.h"
#include "scripting/ScriptTemplate.h"

namespace Osiris {
    Entity Scene::CreateEntity(const std::string& name) {
        const entt::entity handle = m_Registry.create();
        Entity entity(handle, this);
        entity.AddComponent<TagComponent>(name);
        entity.AddComponent<TransformComponent>();
        return entity;
    }

    Entity Scene::FindEntityByName(const std::string& name) {
        auto view = m_Registry.view<TagComponent>();
        for (auto entity : view) {
            auto& tag = view.get<TagComponent>(entity);
            if (tag.name == name) {
                return Entity(entity, this);
            }
        }
        return Entity{};
    }


    std::vector<Entity> Scene::GetAllEntities() {
        std::vector<Entity> result;
        auto view = m_Registry.view<TagComponent>();
        result.reserve(view.size());
        for (auto entity : view) result.emplace_back(entity, this);
        return result;
    }

    void Scene::Render(IRHI* rhi, const Camera& camera) {
        const Frustum frustum = Frustum::FromViewProjection(
            camera.GetProjectionMatrix() * camera.GetViewMatrix()
        );

        m_DrawCallCount = 0;
        m_CulledCount = 0;

        const auto view = m_Registry.view<TransformComponent, MeshComponent, MaterialComponent>();
        for (auto entity : view) {
            auto& transform = view.get<TransformComponent>(entity);
            auto& mesh      = view.get<MeshComponent>(entity);
            auto& material  = view.get<MaterialComponent>(entity);

            glm::mat4 model = transform.GetModelMatrix();
            if (!frustum.IsVisible(mesh.mesh.bounds, model)) {
                m_CulledCount++;
                continue;
            }
            m_DrawCallCount++;
            rhi->SetModelMatrix(model);
            rhi->SetMeshData(mesh.mesh);
            rhi->BindMaterial(material.material);
            rhi->DrawIndexed(mesh.mesh.indexCount);
        }
        //OSIRIS_INFO("Draw calls: {} Culled: {}", drawCalls, culled);
    }

    void Scene::RenderShadows(IRHI* rhi) {
        auto view = m_Registry.view<TransformComponent, MeshComponent>();
        for (auto entity : view) {
            auto& transform = view.get<TransformComponent>(entity);
            auto& mesh      = view.get<MeshComponent>(entity);

            rhi->SetModelMatrix(transform.GetModelMatrix());
            rhi->SetMeshData(mesh.mesh);
            rhi->DrawShadowIndexed(mesh.mesh.indexCount);
        }
    }
    std::vector<SpotLightRenderData> Scene::GatherSpotLights(const glm::vec3& cameraPosition) {
        struct Candidate {
            SpotLightRenderData data;
            float distSq;
            bool  castsShadow;
        };

        std::vector<Candidate> candidates;
        const auto view = m_Registry.view<TransformComponent, SpotLightComponent>();
        for (auto entity : view) {
            auto& transform = view.get<TransformComponent>(entity);
            auto& light     = view.get<SpotLightComponent>(entity);
            if (!light.enabled) continue;

            SpotLightRenderData data{};
            data.position          = transform.position;
            data.direction         = transform.GetForward();
            data.color             = light.color;
            data.intensity         = light.intensity;
            data.innerConeDegrees  = light.innerCone;
            data.outerConeDegrees  = light.outerCone;
            data.range             = light.range;

            const glm::vec3 toCamera = transform.position - cameraPosition;
            candidates.push_back({data, glm::dot(toCamera, toCamera), light.castsShadow});
        }

        // Shadow slots go to the nearest castsShadow=true candidates first.
        std::vector<size_t> shadowCandidates;
        for (size_t i = 0; i < candidates.size(); i++) {
            if (candidates[i].castsShadow) shadowCandidates.push_back(i);
        }
        std::sort(shadowCandidates.begin(), shadowCandidates.end(),
            [&](size_t a, size_t b) { return candidates[a].distSq < candidates[b].distSq; });
        for (size_t slot = 0; slot < shadowCandidates.size() && slot < MAX_SPOT_SHADOW_CASTERS; slot++) {
            candidates[shadowCandidates[slot]].data.shadowIndex = static_cast<int>(slot);
        }

        // Cap at MAX_SPOT_LIGHTS, keeping shadow casters first.
        std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
            const bool aCasts = a.data.shadowIndex >= 0;
            const bool bCasts = b.data.shadowIndex >= 0;
            if (aCasts != bCasts) return aCasts;
            return a.distSq < b.distSq;
        });
        if (candidates.size() > MAX_SPOT_LIGHTS) {
            candidates.resize(MAX_SPOT_LIGHTS);
        }

        std::vector<SpotLightRenderData> result;
        result.reserve(candidates.size());
        for (auto& candidate : candidates) result.push_back(candidate.data);
        return result;
    }

    void Scene::CreatePhysicsBodies(IPhysics* physics) {
        const auto view = m_Registry.view<TransformComponent, ColliderComponent, RigidBodyComponent>();
        for (auto entityHandle : view) {
            RebuildPhysicsBody(Entity(entityHandle, this), physics);
        }
    }

    void Scene::RebuildPhysicsBody(Entity entity, IPhysics* physics) {
        if (!entity.HasComponent<ColliderComponent>() || !entity.HasComponent<RigidBodyComponent>()) return;

        auto& transform  = entity.GetComponent<TransformComponent>();
        auto& collider   = entity.GetComponent<ColliderComponent>();
        auto& rigidBody  = entity.GetComponent<RigidBodyComponent>();

        // Destroying an invalid handle would be a no-op guard failure inside JoltPhysics anyway,
        // but checking here makes the "first body for this entity" case (e.g. RigidBody added
        // via the inspector's Add Component, never through CreatePhysicsBodies) explicit rather
        // than relying on that guard.
        if (rigidBody.bodyHandle.IsValid()) {
            physics->DestroyBody(rigidBody.bodyHandle);
        }

        RigidBodyDesc desc{};
        desc.collider.halfExtents = collider.halfExtents;
        desc.motionType    = rigidBody.motionType;
        desc.position       = transform.position;
        desc.rotationEuler  = transform.rotation;

        rigidBody.bodyHandle = physics->CreateBody(desc);
    }

    void Scene::SyncPhysicsTransforms(IPhysics* physics) {
        const auto view = m_Registry.view<TransformComponent, RigidBodyComponent>();
        for (auto entity : view) {
            auto& transform = view.get<TransformComponent>(entity);
            auto& rigidBody = view.get<RigidBodyComponent>(entity);
            if (rigidBody.motionType == BodyMotionType::Static) continue;

            transform.position = physics->GetBodyPosition(rigidBody.bodyHandle);
            transform.rotation = physics->GetBodyRotationEuler(rigidBody.bodyHandle);
        }
    }

    void Scene::CreateAudioSources(IAudio* audio) {
        const auto view = m_Registry.view<TransformComponent, AudioSourceComponent>();
        for (auto entity : view) {
            auto& transform = view.get<TransformComponent>(entity);
            auto& audioSrc  = view.get<AudioSourceComponent>(entity);

            AudioSourceDesc desc{};
            desc.buffer           = audioSrc.clip;
            desc.gain             = audioSrc.gain;
            desc.pitch            = audioSrc.pitch;
            desc.loop             = audioSrc.loop;
            desc.referenceDistance = audioSrc.referenceDistance;
            desc.maxDistance      = audioSrc.maxDistance;
            desc.rolloffFactor    = audioSrc.rolloffFactor;

            audioSrc.sourceHandle = audio->CreateSource(desc);
            audio->SetSourcePosition(audioSrc.sourceHandle, transform.position);
            if (audioSrc.autoPlay) audio->PlaySource(audioSrc.sourceHandle);
        }
    }

    void Scene::SyncAudioSources(IAudio* audio) {
        const auto view = m_Registry.view<TransformComponent, AudioSourceComponent>();
        for (auto entity : view) {
            auto& transform = view.get<TransformComponent>(entity);
            auto& audioSrc  = view.get<AudioSourceComponent>(entity);
            audio->SetSourcePosition(audioSrc.sourceHandle, transform.position);
        }
    }

    void Scene::CreateScriptInstances(IScripting* scripting) {
        const auto view = m_Registry.view<ScriptComponent>();
        for (auto entity : view) {
            auto& script = view.get<ScriptComponent>(entity);
            CreateScriptFileIfMissing(script.scriptPath);
            script.instanceHandle = scripting->CreateInstance(Entity(entity, this), script.scriptPath);
        }
    }

    int Scene::GetEntityCount() {
        return static_cast<int>(m_Registry.view<TagComponent>().size());
    }

    int Scene::GetDrawCallCount() {
        return m_DrawCallCount;
    }

    int Scene::GetCulledCount() {
        return m_CulledCount;
    }
} // Osiris