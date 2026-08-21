//
// Created by Debreky on 25/07/2026.
//

#ifndef OSIRIS_SCENE_H
#define OSIRIS_SCENE_H
#include <string>
#include <unordered_map>
#include <vector>
#include <entt/entt.hpp>
#include "Entity.h"
#include "Components.h"
#include "core/Engine.h"
#include "renderer/Light.h"

namespace Osiris {
    class Camera;
    class IRHI;
    class IPhysics;
    class IAudio;
    class IScripting;

    class Scene {
    public:
        Entity CreateEntity(const std::string& name);

        // Destroys the live physics body/character/audio source/script instance this entity
        // owns (same per-component teardown as the Scene Inspector's Remove buttons), then
        // removes it from the registry.
        void DestroyEntity(Entity entity, IPhysics* physics, IAudio* audio, IScripting* scripting);

        // Loads a model file (relativePath resolved through AssetManager; glTF via MeshLoader
        // today) and returns one root entity, with one child named "<name>_<index>" per primitive.
        // The one place this logic lives; main.cpp, Lua, and asset-browser spawning all call it.
        Entity SpawnModel(const std::string& name, const std::string& relativePath, IRHI* rhi);

        // Destroys every entity currently in the scene, same per-component teardown as
        // DestroyEntity, one at a time — used before loading a new scene on top of a running one.
        void Clear(IPhysics* physics, IAudio* audio, IScripting* scripting);

        Entity FindEntityByName(const std::string& name);
        std::vector<Entity> GetAllEntities();

        // Keeps ParentComponent and ChildrenComponent in sync while preserving world transform.
        // An invalid newParent unparents; requests that would create a cycle are ignored.
        void SetParent(Entity child, Entity newParent);
        Entity GetParent(Entity entity);
        std::vector<Entity> GetChildren(Entity entity);
        glm::mat4 GetWorldTransform(Entity entity) const;

        void Update(float deltaTime);
        void Render(IRHI* rhi, const Camera& camera);
        void RenderShadows(IRHI* rhi);

        // Spawns a physics body for every entity with both ColliderComponent and RigidBodyComponent.
        void CreatePhysicsBodies(IPhysics* physics);

        // Destroys (if valid) and recreates one entity's physics body — Jolt has no in-place
        // shape/motion-type change. CreatePhysicsBodies is this called once per matching entity.
        void RebuildPhysicsBody(Entity entity, IPhysics* physics);

        // Writes each non-Static body's live world pose back into its local TransformComponent.
        void SyncPhysicsTransforms(IPhysics* physics);

        // Creates a live OpenAL source per AudioSourceComponent entity; auto-plays if autoPlay=true.
        void CreateAudioSources(IAudio* audio);

        // Keeps each audio source's 3D position current — call once per frame.
        void SyncAudioSources(IAudio* audio);

        // Starts every autoPlay=true source — call when Play mode starts.
        void PlayAutoPlayAudioSources(IAudio* audio);

        // Stops every source unconditionally — call when Play mode ends.
        void StopAllAudioSources(IAudio* audio);

        // Creates a script instance per ScriptComponent entity.
        void CreateScriptInstances(IScripting* scripting);

        // Spawns a Jolt CharacterVirtual per CharacterComponent entity.
        void CreateCharacters(IPhysics* physics);

        // Destroys (if valid) and recreates one entity's character — CharacterComponent's
        // equivalent of RebuildPhysicsBody.
        void RebuildCharacter(Entity entity, IPhysics* physics);

        // Writes each character's live world position back into its local TransformComponent.
        void SyncCharacterTransforms(IPhysics* physics);

        // Play mode snapshot/restore — Transform only, not a full scene undo.
        void CapturePlaySnapshot();
        void RestorePlaySnapshot(IPhysics* physics);

        // Destroys and recreates every script instance, so OnStart reruns fresh each Play session.
        void ResetScriptInstances(IScripting* scripting);

        // The entity Play mode's camera follows (prefers isPrimary=true), or an invalid Entity.
        Entity FindCameraEntity();

        std::vector<SpotLightRenderData> GatherSpotLights(const glm::vec3& cameraPosition);

        void PreRender(IRHI * irhi);

        int GetEntityCount();
        int GetDrawCallCount();
        int GetCulledCount();

    private:
        friend class Entity;
        entt::registry m_Registry;

        uint32_t m_DrawCallCount;
        uint32_t m_CulledCount;

        // Play mode snapshot — see CapturePlaySnapshot/RestorePlaySnapshot.
        std::unordered_map<entt::entity, TransformComponent> m_PlaySnapshot;
    };
    template<typename T, typename... Args>
    T& Entity::AddComponent(Args&&... args) {
        return m_Scene->m_Registry.emplace<T>(m_Handle, std::forward<Args>(args)...);
    }

    template<typename T>
    T& Entity::GetComponent() {
        return m_Scene->m_Registry.get<T>(m_Handle);
    }

    template<typename T>
    bool Entity::HasComponent() const {
        return m_Scene->m_Registry.all_of<T>(m_Handle);
    }

    template<typename T>
    void Entity::RemoveComponent() {
        m_Scene->m_Registry.remove<T>(m_Handle);
    }
} // Osiris

#endif //OSIRIS_SCENE_H
