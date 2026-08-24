//
// Created by Debreky on 25/07/2026.
//

#ifndef OSIRIS_COMPONENTS_H
#define OSIRIS_COMPONENTS_H

#include <vector>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include "renderer/MeshType.h"
#include "rhi/RHITypes.h"
#include "physics/PhysicsTypes.h"
#include "audio/AudioTypes.h"
#include "scripting/ScriptTypes.h"

namespace Osiris {
    class Scene;

    struct TransformComponent {
        glm::vec3 position = {0,0,0};
        glm::vec3 rotation = {0,0,0}; // euler angles
        glm::vec3 scale    = {1,1,1};
        glm::mat4 GetModelMatrix() const;

        glm::vec3 GetForward() const; // local rotation-only; rest direction is (0,-1,0)

        // Horizontal-only (ignores pitch/roll, never has a Y component): built from rotation.y
        // alone, same yaw convention tank_controller.lua's own forward-vector math already uses.
        // For WASD-style ground movement, where a fixed walk speed regardless of look angle is
        // the expected behavior, not GetForward()'s full 3D direction (meant for spot lights).
        glm::vec3 GetForwardXZ() const;
        glm::vec3 GetRightXZ() const;

        // Inverse of GetModelMatrix(): decomposes matrix into position/rotation/scale, matching
        // this type's Rx*Ry*Rz composition exactly. Used wherever a resolved world (or
        // parent-relative) matrix needs writing back into a plain TRS transform — SetParent,
        // physics sync, the editor gizmo.
        void SetFromMatrix(const glm::mat4& matrix);

        // Just the rotation half of SetFromMatrix, for callers that don't have (or don't want to
        // overwrite) a whole TransformComponent — e.g. building a RigidBodyDesc, or updating
        // position without disturbing scale. `reference` picks whichever of the two
        // mathematically-equivalent Euler solutions stays closest to it, so repeated calls don't
        // jump between them.
        static glm::vec3 ExtractRotation(const glm::mat4& matrix, const glm::vec3& reference);
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

    struct ParentComponent {
        ParentComponent() = default;
        bool HasParent() const { return m_Parent != entt::null; }

    private:
        friend class Scene;
        entt::entity m_Parent = entt::null;
    };

    struct ChildrenComponent {
        ChildrenComponent() = default;
        size_t GetCount() const { return m_Children.size(); }

    private:
        friend class Scene;
        std::vector<entt::entity> m_Children;
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

    // Box shape only for now. Half-extents match BoxColliderDesc's units (meters).
    struct ColliderComponent {
        glm::vec3 halfExtents = {0.5f, 0.5f, 0.5f};
    };

    // Paired with ColliderComponent; Scene::CreatePhysicsBodies fills in bodyHandle.
    struct RigidBodyComponent {
        BodyMotionType    motionType          = BodyMotionType::Static;
        bool              lockRotationToYAxis = false;
        bool              isSensor            = false;
        PhysicsBodyHandle bodyHandle;
    };

    // Position comes from the entity's resolved world transform, same as SpotLightComponent.
    struct AudioSourceComponent {
        AudioBufferHandle clip;
        std::string clipPath; // relative to AssetManager's root — clip itself has no provenance
        float gain              = 1.0f;
        float pitch             = 1.0f;
        bool  loop               = false;
        bool  autoPlay           = true;
        float referenceDistance = 1.0f;
        float maxDistance       = 30.0f;
        float rolloffFactor     = 1.0f;
        AudioSourceHandle sourceHandle;
    };

    // Attaches Lua behavior to an entity. Scene::CreateScriptInstances loads scriptPath, writing
    // a fresh OnStart/OnUpdate/OnFixedUpdate stub if the file doesn't exist yet.
    struct ScriptComponent {
        std::string scriptPath;
        ScriptInstanceHandle instanceHandle;
    };

    // Marks an entity Play mode's camera can follow. Scene::FindCameraEntity() prefers isPrimary=true.
    struct CameraComponent {
        float eyeHeight  = 1.5f;
        bool  isPrimary  = true;
    };

    // Gives this entity a Jolt CharacterVirtual (Scene::CreateCharacters), distinct from RigidBodyComponent.
    struct CharacterComponent {
        float radius           = 0.3f;
        float height            = 1.8f; // capsule cylinder height, excluding the two hemispherical caps
        float maxSlopeAngleDeg = 45.0f;
        float mass              = 70.0f;
        CharacterHandle characterHandle;
    };

    // Records which glTF a model root/primitive came from — Mesh/MaterialComponent only hold
    // opaque GPU handles, so SceneLoader::Save needs this to recover the source path.
    struct ModelSourceComponent {
        std::string relativePath;
    };
}

#endif //OSIRIS_COMPONENTS_H
