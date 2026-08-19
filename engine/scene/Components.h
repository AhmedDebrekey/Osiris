//
// Created by Debreky on 25/07/2026.
//

#ifndef OSIRIS_COMPONENTS_H
#define OSIRIS_COMPONENTS_H

#include <glm/glm.hpp>

#include "renderer/MeshType.h"
#include "rhi/RHITypes.h"
#include "physics/PhysicsTypes.h"
#include "audio/AudioTypes.h"
#include "scripting/ScriptTypes.h"

namespace Osiris {
    struct TransformComponent {
        glm::vec3 position = {0,0,0};
        glm::vec3 rotation = {0,0,0}; // euler angles
        glm::vec3 scale    = {1,1,1};
        glm::mat4 GetModelMatrix() const;

        glm::vec3 GetForward() const; // rotation-only; rest direction is (0,-1,0)
    };

    struct MeshComponent {
        Mesh mesh;
    };

    struct MaterialComponent {
        MaterialHandle material;
        // more later for PBR
    };

    struct TagComponent {
        std::string name;
    };

    struct SpotLightComponent {
        glm::vec3 color     = glm::vec3(1.0f);
        float intensity     = 1.0f;
        float innerCone     = 12.5f;
        float outerCone     = 17.5f;
        float range         = 10.f;
        bool enabled        = true;
        bool castsShadow    = true;
    };

    // Box shape only for now — extend once a second concrete shape (sphere/capsule
    // props) actually needs it. Half-extents match BoxColliderDesc's units (meters).
    struct ColliderComponent {
        glm::vec3 halfExtents = {0.5f, 0.5f, 0.5f};
    };

    // Paired with ColliderComponent; split into two components (rather than one) to mirror
    // the existing MeshComponent/MaterialComponent split. Scene::CreatePhysicsBodies fills in
    // bodyHandle from the entity's TransformComponent + ColliderComponent at scene setup time.
    struct RigidBodyComponent {
        BodyMotionType    motionType = BodyMotionType::Static;
        PhysicsBodyHandle bodyHandle;
    };

    // Position comes from the entity's TransformComponent (same pattern as SpotLightComponent
    // not storing its own position). Scene::CreateAudioSources fills in sourceHandle at scene
    // setup time; Scene::SyncAudioSources keeps the live OpenAL source's position current.
    struct AudioSourceComponent {
        AudioBufferHandle clip;
        float gain              = 1.0f;
        float pitch             = 1.0f;
        bool  loop               = false;
        bool  autoPlay           = true;
        float referenceDistance = 1.0f;
        float maxDistance       = 30.0f;
        float rolloffFactor     = 1.0f;
        AudioSourceHandle sourceHandle;
    };

    // Attaches Lua behavior to an entity. Scene::CreateScriptInstances loads scriptPath (writing
    // a fresh OnStart/OnUpdate/OnFixedUpdate stub there first if the file doesn't exist yet) and
    // fills in instanceHandle at scene setup time — same pattern as RigidBodyComponent/
    // AudioSourceComponent's bodyHandle/sourceHandle.
    struct ScriptComponent {
        std::string scriptPath;
        ScriptInstanceHandle instanceHandle;
    };
}

#endif //OSIRIS_COMPONENTS_H