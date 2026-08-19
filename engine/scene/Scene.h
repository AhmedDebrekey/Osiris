//
// Created by Debreky on 25/07/2026.
//

#ifndef OSIRIS_SCENE_H
#define OSIRIS_SCENE_H
#include <string>
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
        Entity FindEntityByName(const std::string& name);
        std::vector<Entity> GetAllEntities();
        void Update(float deltaTime);
        void Render(IRHI* rhi, const Camera& camera);
        void RenderShadows(IRHI* rhi);

        // Spawns a physics body for every entity that has both a ColliderComponent and a
        // RigidBodyComponent — call once after the scene's entities are set up.
        void CreatePhysicsBodies(IPhysics* physics);

        // Destroys (if valid) and recreates a single entity's physics body from its current
        // Transform/Collider/RigidBody values. Jolt has no in-place "resize this body's shape" or
        // "change this body's motion type" — destroy-and-recreate is the direct fix, not a
        // shortcut. No-op if the entity is missing either ColliderComponent or RigidBodyComponent.
        // CreatePhysicsBodies itself is just this called once per matching entity; SceneInspector
        // Panel calls it again whenever the user edits ColliderComponent::halfExtents or
        // RigidBodyComponent::motionType so the live body actually reflects the edit.
        void RebuildPhysicsBody(Entity entity, IPhysics* physics);

        // Writes each Dynamic/Kinematic body's live position + rotation back into
        // TransformComponent — call once per frame, after IPhysics::Update(). Static bodies
        // never move, so they're skipped.
        void SyncPhysicsTransforms(IPhysics* physics);

        // Creates a live OpenAL source for every entity with a TransformComponent +
        // AudioSourceComponent — call once after the scene's entities are set up. Auto-plays
        // any source with autoPlay=true.
        void CreateAudioSources(IAudio* audio);

        // Keeps each audio source's 3D position current — call once per frame.
        void SyncAudioSources(IAudio* audio);

        // Creates a script instance for every entity with a ScriptComponent — call once after
        // the scene's entities are set up. Per-frame OnUpdate/OnFixedUpdate dispatch happens
        // directly through IScripting::Update/FixedUpdate (same as IPhysics::Update), not here.
        void CreateScriptInstances(IScripting* scripting);

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