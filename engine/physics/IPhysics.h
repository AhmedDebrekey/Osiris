#ifndef OSIRIS_IPHYSICS_H
#define OSIRIS_IPHYSICS_H

#include "PhysicsTypes.h"

namespace Osiris {
    // Backend-agnostic physics interface, mirroring IRHI: engine/scene and games/testbed
    // only ever talk to IPhysics, never to Jolt types directly.
    class IPhysics {
    public:
        virtual ~IPhysics() = default;

        virtual bool Init() = 0;
        virtual void Shutdown() = 0;

        // Steps the simulation using a fixed internal timestep (accumulator pattern) —
        // safe to call once per rendered frame with that frame's (variable) deltaTime.
        virtual void Update(float deltaTime) = 0;

        virtual PhysicsBodyHandle CreateBody(const RigidBodyDesc& desc) = 0;
        virtual void DestroyBody(PhysicsBodyHandle handle) = 0;

        // World-space impulse (mass * velocity change), applied instantly. Mass comes from
        // Jolt's own shape-volume x density computation — RigidBodyDesc has no mass override.
        // Only meaningful for Dynamic bodies; Static/Kinematic bodies ignore impulses in Jolt.
        virtual void ApplyImpulse(PhysicsBodyHandle handle, const glm::vec3& impulse) = 0;

        // Live body origin (matches whatever position RigidBodyDesc created it at). Only
        // meaningful for Dynamic/Kinematic bodies — static bodies never move, so nothing
        // currently reads this for them.
        virtual glm::vec3 GetBodyPosition(PhysicsBodyHandle handle) const = 0;

        // Live body orientation as Euler degrees, using the same Rx*Ry*Rz composition order
        // as TransformComponent::GetModelMatrix (see the JoltPhysics.cpp implementation for
        // the decomposition derivation — deliberately not glm's generic euler-angle helpers,
        // which don't use this project's specific composition order).
        virtual glm::vec3 GetBodyRotationEuler(PhysicsBodyHandle handle) const = 0;

        virtual CharacterHandle CreateCharacter(const CharacterDesc& desc) = 0;
        virtual void DestroyCharacter(CharacterHandle handle) = 0;

        // Sets the character's desired horizontal velocity (world-space, Y ignored) and
        // whether a jump was requested this frame; actual gravity/ground-snap/step-up
        // integration happens internally on the next fixed physics step.
        virtual void SetCharacterDesiredVelocity(CharacterHandle handle, const glm::vec3& horizontalVelocity, bool jump) = 0;
        virtual glm::vec3 GetCharacterPosition(CharacterHandle handle) const = 0;
        virtual bool IsCharacterGrounded(CharacterHandle handle) const = 0;
    };
}

#endif //OSIRIS_IPHYSICS_H
