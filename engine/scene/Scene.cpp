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
#include "assets/MeshLoader.h"
#include "core/AssetManager.h"

namespace Osiris {
    Entity Scene::CreateEntity(const std::string& name) {
        const entt::entity handle = m_Registry.create();
        Entity entity(handle, this);
        entity.AddComponent<TagComponent>(name);
        entity.AddComponent<TransformComponent>();
        return entity;
    }

    void Scene::DestroyEntity(Entity entity, IPhysics* physics, IAudio* audio, IScripting* scripting) {
        if (entity.GetScene() != this || !m_Registry.valid(entity.GetHandle())) return;

        std::vector<entt::entity> subtree;
        subtree.push_back(entity.GetHandle());
        for (size_t i = 0; i < subtree.size(); i++) {
            const entt::entity current = subtree[i];
            if (const auto* children = m_Registry.try_get<ChildrenComponent>(current)) {
                subtree.insert(subtree.end(), children->m_Children.begin(), children->m_Children.end());
            }
        }

        for (auto it = subtree.rbegin(); it != subtree.rend(); ++it) {
            if (!m_Registry.valid(*it)) continue;

            Entity current(*it, this);
            SetParent(current, Entity{});

            if (current.HasComponent<RigidBodyComponent>()) {
                auto& rigidBody = current.GetComponent<RigidBodyComponent>();
                if (rigidBody.bodyHandle.IsValid()) physics->DestroyBody(rigidBody.bodyHandle);
            }
            if (current.HasComponent<CharacterComponent>()) {
                auto& character = current.GetComponent<CharacterComponent>();
                if (character.characterHandle.IsValid()) physics->DestroyCharacter(character.characterHandle);
            }
            if (current.HasComponent<AudioSourceComponent>()) {
                auto& audioSrc = current.GetComponent<AudioSourceComponent>();
                if (audioSrc.sourceHandle.IsValid()) audio->DestroySource(audioSrc.sourceHandle);
            }
            if (current.HasComponent<ScriptComponent>()) {
                auto& script = current.GetComponent<ScriptComponent>();
                if (script.instanceHandle.IsValid()) scripting->DestroyInstance(script.instanceHandle);
            }

            m_PlaySnapshot.erase(*it);
            m_Registry.destroy(*it);
        }
    }

    void Scene::QueueDestroyEntity(Entity entity) {
        if (entity.GetScene() != this || !m_Registry.valid(entity.GetHandle())) return;
        if (std::find(m_DestroyQueue.begin(), m_DestroyQueue.end(), entity.GetHandle()) != m_DestroyQueue.end()) return;
        m_DestroyQueue.push_back(entity.GetHandle());
    }

    void Scene::FlushDestroyQueue(IPhysics* physics, IAudio* audio, IScripting* scripting) {
        std::vector<entt::entity> destroyQueue;
        destroyQueue.swap(m_DestroyQueue);
        for (entt::entity entityHandle : destroyQueue) {
            DestroyEntity(Entity(entityHandle, this), physics, audio, scripting);
        }
    }

    void Scene::Clear(IPhysics* physics, IAudio* audio, IScripting* scripting) {
        m_DestroyQueue.clear();
        for (Entity entity : GetAllEntities()) {
            DestroyEntity(entity, physics, audio, scripting);
        }
    }

    Entity Scene::SpawnModel(const std::string& name, const std::string& relativePath, IRHI* rhi) {
        std::vector<GltfNode> nodes = MeshLoader::LoadFromGLTF(AssetManager::GetPath(relativePath), rhi);
        if (nodes.empty()) return Entity{};

        Entity root = CreateEntity(name);
        root.AddComponent<ModelSourceComponent>(relativePath);

        std::vector<Entity> nodeEntities;
        nodeEntities.reserve(nodes.size());
        for (std::size_t i = 0; i < nodes.size(); i++) {
            const std::string nodeName = name + "_"
                + (nodes[i].name.empty() ? "Node" : nodes[i].name)
                + "_" + std::to_string(i);
            Entity entity = CreateEntity(nodeName);
            entity.AddComponent<ModelSourceComponent>(relativePath);
            if (!nodes[i].primitives.empty()) {
                entity.AddComponent<MeshComponent>(nodes[i].primitives[0].mesh);
                entity.AddComponent<MaterialComponent>(nodes[i].primitives[0].material);
            }
            nodeEntities.push_back(entity);
        }

        for (std::size_t i = 0; i < nodes.size(); i++) {
            Entity parent = root;
            if (nodes[i].parentIndex.has_value() && nodes[i].parentIndex.value() < nodeEntities.size()) {
                parent = nodeEntities[nodes[i].parentIndex.value()];
            }
            SetParent(nodeEntities[i], parent, false);
            nodeEntities[i].GetComponent<TransformComponent>().SetFromMatrix(nodes[i].localTransform);
        }

        for (std::size_t i = 0; i < nodes.size(); i++) {
            const std::string& nodeName = nodeEntities[i].GetComponent<TagComponent>().name;
            for (std::size_t primitiveIndex = 1;
                 primitiveIndex < nodes[i].primitives.size(); primitiveIndex++) {
                Entity primitiveEntity = CreateEntity(
                    nodeName + "_Primitive_" + std::to_string(primitiveIndex));
                primitiveEntity.AddComponent<MeshComponent>(nodes[i].primitives[primitiveIndex].mesh);
                primitiveEntity.AddComponent<MaterialComponent>(nodes[i].primitives[primitiveIndex].material);
                primitiveEntity.AddComponent<ModelSourceComponent>(relativePath);
                // false: primitiveEntity was just default-constructed at identity, which is
                // already the right local transform relative to nodeEntities[i] (it's just
                // another material slice of the same node's mesh) — no need to preserve/rewrite it.
                SetParent(primitiveEntity, nodeEntities[i], false);
            }
        }
        return root;
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

    void Scene::SetParent(Entity child, Entity newParent, bool preserveWorldTransform) {
        if (child.GetScene() != this || !m_Registry.valid(child.GetHandle())) return;

        const entt::entity childHandle = child.GetHandle();
        entt::entity newParentHandle = entt::null;
        if (newParent.IsValid()) {
            if (newParent.GetScene() != this || !m_Registry.valid(newParent.GetHandle())) return;
            newParentHandle = newParent.GetHandle();

            for (entt::entity ancestor = newParentHandle; ancestor != entt::null;) {
                if (ancestor == childHandle) return;
                if (!m_Registry.valid(ancestor)) break;

                const auto* parent = m_Registry.try_get<ParentComponent>(ancestor);
                ancestor = parent ? parent->m_Parent : entt::null;
            }
        }

        auto* currentParent = m_Registry.try_get<ParentComponent>(childHandle);
        if (currentParent && currentParent->m_Parent == newParentHandle) return;

        glm::mat4 worldTransform(1.0f);
        if (preserveWorldTransform) worldTransform = GetWorldTransform(child);

        if (currentParent) {
            const entt::entity oldParentHandle = currentParent->m_Parent;
            if (m_Registry.valid(oldParentHandle)) {
                if (auto* oldChildren = m_Registry.try_get<ChildrenComponent>(oldParentHandle)) {
                    std::erase(oldChildren->m_Children, childHandle);
                    if (oldChildren->m_Children.empty()) {
                        m_Registry.remove<ChildrenComponent>(oldParentHandle);
                    }
                }
            }
            m_Registry.remove<ParentComponent>(childHandle);
        }

        if (newParentHandle == entt::null) {
            if (preserveWorldTransform) {
                if (auto* transform = m_Registry.try_get<TransformComponent>(childHandle)) {
                    transform->SetFromMatrix(worldTransform);
                }
            }
            return;
        }

        auto& parent = m_Registry.emplace<ParentComponent>(childHandle);
        parent.m_Parent = newParentHandle;

        auto& children = m_Registry.get_or_emplace<ChildrenComponent>(newParentHandle);
        children.m_Children.push_back(childHandle);

        if (preserveWorldTransform) {
            if (auto* transform = m_Registry.try_get<TransformComponent>(childHandle)) {
                // Non-uniformly scaled parents can introduce shear that local TRS cannot preserve exactly.
                transform->SetFromMatrix(glm::inverse(GetWorldTransform(newParent)) * worldTransform);
            }
        }
    }

    Entity Scene::GetParent(Entity entity) {
        if (entity.GetScene() != this || !m_Registry.valid(entity.GetHandle())) return Entity{};

        const auto* parent = m_Registry.try_get<ParentComponent>(entity.GetHandle());
        if (!parent || !m_Registry.valid(parent->m_Parent)) return Entity{};
        return Entity(parent->m_Parent, this);
    }

    std::vector<Entity> Scene::GetChildren(Entity entity) {
        std::vector<Entity> result;
        if (entity.GetScene() != this || !m_Registry.valid(entity.GetHandle())) return result;

        const auto* children = m_Registry.try_get<ChildrenComponent>(entity.GetHandle());
        if (!children) return result;

        result.reserve(children->m_Children.size());
        for (entt::entity child : children->m_Children) {
            if (m_Registry.valid(child)) result.emplace_back(child, this);
        }
        return result;
    }

    glm::mat4 Scene::GetWorldTransform(Entity entity) const {
        if (entity.GetScene() != this || !m_Registry.valid(entity.GetHandle())) return glm::mat4(1.0f);

        std::vector<entt::entity> chain;
        for (entt::entity current = entity.GetHandle(); current != entt::null;) {
            if (!m_Registry.valid(current)) break;
            chain.push_back(current);

            const auto* parent = m_Registry.try_get<ParentComponent>(current);
            current = parent ? parent->m_Parent : entt::null;
        }

        glm::mat4 worldTransform(1.0f);
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            if (const auto* transform = m_Registry.try_get<TransformComponent>(*it)) {
                worldTransform *= transform->GetModelMatrix();
            }
        }
        return worldTransform;
    }

    void Scene::Render(IRHI* rhi, const Camera& camera) {
        const Frustum frustum = Frustum::FromViewProjection(
            camera.GetProjectionMatrix() * camera.GetViewMatrix()
        );

        m_DrawCallCount = 0;
        m_CulledCount = 0;

        const auto view = m_Registry.view<TransformComponent, MeshComponent, MaterialComponent>();
        for (auto entity : view) {
            auto& mesh      = view.get<MeshComponent>(entity);
            auto& material  = view.get<MaterialComponent>(entity);

            const glm::mat4 model = GetWorldTransform(Entity(entity, this));
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
            auto& mesh      = view.get<MeshComponent>(entity);

            rhi->SetModelMatrix(GetWorldTransform(Entity(entity, this)));
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
            auto& light     = view.get<SpotLightComponent>(entity);
            if (!light.enabled) continue;

            const glm::mat4 worldTransform = GetWorldTransform(Entity(entity, this));
            const glm::vec3 worldPosition = glm::vec3(worldTransform[3]);
            const glm::vec3 worldDirection = glm::normalize(glm::vec3(
                worldTransform * glm::vec4(0.0f, -1.0f, 0.0f, 0.0f)));

            SpotLightRenderData data{};
            data.position          = worldPosition;
            data.direction         = worldDirection;
            data.color             = light.color;
            data.intensity         = light.intensity;
            data.innerConeDegrees  = light.innerCone;
            data.outerConeDegrees  = light.outerCone;
            data.range             = light.range;

            const glm::vec3 toCamera = worldPosition - cameraPosition;
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

        if (rigidBody.bodyHandle.IsValid()) {
            physics->DestroyBody(rigidBody.bodyHandle);
        }

        RigidBodyDesc desc{};
        desc.collider.halfExtents = collider.halfExtents;
        desc.motionType          = rigidBody.motionType;
        desc.lockRotationToYAxis = rigidBody.lockRotationToYAxis;
        desc.userData            = static_cast<uint64_t>(entity.GetHandle());
        Entity parent = GetParent(entity);
        if (parent.IsValid()) {
            const glm::mat4 worldTransform = GetWorldTransform(entity);
            desc.position      = glm::vec3(worldTransform[3]);
            desc.rotationEuler = TransformComponent::ExtractRotation(worldTransform, transform.rotation);
        } else {
            desc.position      = transform.position;
            desc.rotationEuler = transform.rotation;
        }

        rigidBody.bodyHandle = physics->CreateBody(desc);
    }

    void Scene::SyncPhysicsTransforms(IPhysics* physics) {
        const auto view = m_Registry.view<TransformComponent, RigidBodyComponent>();
        for (auto entity : view) {
            auto& transform = view.get<TransformComponent>(entity);
            auto& rigidBody = view.get<RigidBodyComponent>(entity);
            if (rigidBody.motionType == BodyMotionType::Static) continue;

            const glm::vec3 worldPosition = physics->GetBodyPosition(rigidBody.bodyHandle);
            const glm::vec3 worldRotation = physics->GetBodyRotationEuler(rigidBody.bodyHandle);
            Entity current(entity, this);
            Entity parent = GetParent(current);
            if (!parent.IsValid()) {
                transform.position = worldPosition;
                transform.rotation = worldRotation;
                continue;
            }

            TransformComponent worldTransform;
            worldTransform.position = worldPosition;
            worldTransform.rotation = worldRotation;
            const glm::mat4 localTransform = glm::inverse(GetWorldTransform(parent))
                * worldTransform.GetModelMatrix();
            transform.position = glm::vec3(localTransform[3]);
            // Non-uniformly scaled ancestors can introduce shear that local TRS cannot represent exactly.
            transform.rotation = TransformComponent::ExtractRotation(localTransform, transform.rotation);
        }
    }

    void Scene::DispatchCollisionEvents(IPhysics* physics, IScripting* scripting) {
        for (const auto& [aHandleValue, bHandleValue] : physics->DrainCollisionEvents()) {
            const entt::entity aHandle = static_cast<entt::entity>(aHandleValue);
            const entt::entity bHandle = static_cast<entt::entity>(bHandleValue);

            if (m_Registry.valid(aHandle) && m_Registry.valid(bHandle)) {
                Entity a(aHandle, this);
                if (a.HasComponent<ScriptComponent>()) {
                    scripting->DispatchCollision(
                        a.GetComponent<ScriptComponent>().instanceHandle,
                        Entity(bHandle, this));
                }
            }

            if (m_Registry.valid(aHandle) && m_Registry.valid(bHandle)) {
                Entity b(bHandle, this);
                if (b.HasComponent<ScriptComponent>()) {
                    scripting->DispatchCollision(
                        b.GetComponent<ScriptComponent>().instanceHandle,
                        Entity(aHandle, this));
                }
            }
        }
    }

    void Scene::CreateAudioSources(IAudio* audio) {
        const auto view = m_Registry.view<TransformComponent, AudioSourceComponent>();
        for (auto entity : view) {
            auto& audioSrc  = view.get<AudioSourceComponent>(entity);
            if (!audioSrc.clip.IsValid()) continue; // no clip assigned yet — nothing to create a source from

            AudioSourceDesc desc{};
            desc.buffer           = audioSrc.clip;
            desc.gain             = audioSrc.gain;
            desc.pitch            = audioSrc.pitch;
            desc.loop             = audioSrc.loop;
            desc.referenceDistance = audioSrc.referenceDistance;
            desc.maxDistance      = audioSrc.maxDistance;
            desc.rolloffFactor    = audioSrc.rolloffFactor;

            audioSrc.sourceHandle = audio->CreateSource(desc);
            const glm::vec3 worldPosition = glm::vec3(GetWorldTransform(Entity(entity, this))[3]);
            audio->SetSourcePosition(audioSrc.sourceHandle, worldPosition);
            if (audioSrc.autoPlay) audio->PlaySource(audioSrc.sourceHandle);
        }
    }

    void Scene::SyncAudioSources(IAudio* audio) {
        const auto view = m_Registry.view<TransformComponent, AudioSourceComponent>();
        for (auto entity : view) {
            auto& audioSrc  = view.get<AudioSourceComponent>(entity);
            const glm::vec3 worldPosition = glm::vec3(GetWorldTransform(Entity(entity, this))[3]);
            audio->SetSourcePosition(audioSrc.sourceHandle, worldPosition);
        }
    }

    void Scene::PlayAutoPlayAudioSources(IAudio* audio) {
        const auto view = m_Registry.view<AudioSourceComponent>();
        for (auto entity : view) {
            auto& audioSrc = view.get<AudioSourceComponent>(entity);
            if (audioSrc.autoPlay && audioSrc.sourceHandle.IsValid()) {
                audio->PlaySource(audioSrc.sourceHandle);
            }
        }
    }

    void Scene::StopAllAudioSources(IAudio* audio) {
        const auto view = m_Registry.view<AudioSourceComponent>();
        for (auto entity : view) {
            auto& audioSrc = view.get<AudioSourceComponent>(entity);
            if (audioSrc.sourceHandle.IsValid()) {
                audio->StopSource(audioSrc.sourceHandle);
            }
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

    void Scene::CreateCharacters(IPhysics* physics) {
        const auto view = m_Registry.view<TransformComponent, CharacterComponent>();
        for (auto entityHandle : view) {
            RebuildCharacter(Entity(entityHandle, this), physics);
        }
    }

    void Scene::RebuildCharacter(Entity entity, IPhysics* physics) {
        if (!entity.HasComponent<CharacterComponent>()) return;

        auto& transform = entity.GetComponent<TransformComponent>();
        auto& character = entity.GetComponent<CharacterComponent>();

        if (character.characterHandle.IsValid()) {
            physics->DestroyCharacter(character.characterHandle);
        }

        CharacterDesc desc{};
        desc.radius           = character.radius;
        desc.height            = character.height;
        desc.maxSlopeAngleDeg  = character.maxSlopeAngleDeg;
        desc.mass              = character.mass;
        Entity parent = GetParent(entity);
        desc.position = parent.IsValid()
            ? glm::vec3(GetWorldTransform(entity)[3])
            : transform.position;

        character.characterHandle = physics->CreateCharacter(desc);
    }

    void Scene::SyncCharacterTransforms(IPhysics* physics) {
        const auto view = m_Registry.view<TransformComponent, CharacterComponent>();
        for (auto entity : view) {
            auto& transform = view.get<TransformComponent>(entity);
            auto& character = view.get<CharacterComponent>(entity);
            if (!character.characterHandle.IsValid()) continue;

            const glm::vec3 worldPosition = physics->GetCharacterPosition(character.characterHandle);
            Entity parent = GetParent(Entity(entity, this));
            if (!parent.IsValid()) {
                transform.position = worldPosition;
                continue;
            }

            transform.position = glm::vec3(
                glm::inverse(GetWorldTransform(parent)) * glm::vec4(worldPosition, 1.0f));
        }
    }

    void Scene::CapturePlaySnapshot() {
        m_PlaySnapshot.clear();

        // Static bodies never move, so there's nothing to restore for them.
        const auto rbView = m_Registry.view<TransformComponent, RigidBodyComponent>();
        for (auto entity : rbView) {
            if (rbView.get<RigidBodyComponent>(entity).motionType == BodyMotionType::Static) continue;
            m_PlaySnapshot[entity] = rbView.get<TransformComponent>(entity);
        }

        const auto charView = m_Registry.view<TransformComponent, CharacterComponent>();
        for (auto entity : charView) {
            m_PlaySnapshot[entity] = charView.get<TransformComponent>(entity);
        }
    }

    void Scene::RestorePlaySnapshot(IPhysics* physics) {
        for (auto& [entityHandle, transform] : m_PlaySnapshot) {
            if (!m_Registry.valid(entityHandle) || !m_Registry.all_of<TransformComponent>(entityHandle)) continue;

            m_Registry.get<TransformComponent>(entityHandle) = transform;

            Entity entity(entityHandle, this);
            if (entity.HasComponent<RigidBodyComponent>()) {
                RebuildPhysicsBody(entity, physics);
            }
            if (entity.HasComponent<CharacterComponent>()) {
                RebuildCharacter(entity, physics);
            }
        }
        m_PlaySnapshot.clear();
    }

    void Scene::ResetScriptInstances(IScripting* scripting) {
        const auto view = m_Registry.view<ScriptComponent>();
        for (auto entity : view) {
            auto& script = view.get<ScriptComponent>(entity);
            if (script.instanceHandle.IsValid()) {
                scripting->DestroyInstance(script.instanceHandle);
            }
        }
        CreateScriptInstances(scripting);
    }

    Entity Scene::FindCameraEntity() {
        auto view = m_Registry.view<CameraComponent>();
        entt::entity firstFound = entt::null;
        for (auto entity : view) {
            if (firstFound == entt::null) firstFound = entity;
            if (view.get<CameraComponent>(entity).isPrimary) {
                return Entity(entity, this);
            }
        }
        if (firstFound != entt::null) return Entity(firstFound, this);
        return Entity{};
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
