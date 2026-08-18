#ifndef OSIRIS_PHYSICSTYPES_H
#define OSIRIS_PHYSICSTYPES_H

#include <glm/glm.hpp>

#include "rhi/RHITypes.h" // reuse the generic Handle<Tag> template — physics bodies aren't
                           // GPU resources, but the same slot-handle pattern applies.

namespace Osiris {
    struct PhysicsBodyTag {};
    struct CharacterTag   {};

    using PhysicsBodyHandle = Handle<PhysicsBodyTag>;
    using CharacterHandle   = Handle<CharacterTag>;

    enum class BodyMotionType {
        Static,
        Kinematic,
        Dynamic,
    };

    // Box collider only for now — extend with more shapes once a second concrete
    // use case (sphere/capsule props) actually needs them.
    struct BoxColliderDesc {
        glm::vec3 halfExtents = {0.5f, 0.5f, 0.5f};
    };

    struct RigidBodyDesc {
        BoxColliderDesc collider;
        BodyMotionType  motionType    = BodyMotionType::Static;
        glm::vec3       position      = {0.0f, 0.0f, 0.0f};
        glm::vec3       rotationEuler = {0.0f, 0.0f, 0.0f}; // degrees, matches TransformComponent
    };

    struct CharacterDesc {
        float radius          = 0.3f;
        float height           = 1.8f; // capsule cylinder height, excluding the two hemispherical caps
        float maxSlopeAngleDeg = 45.0f;
        float mass             = 70.0f;
        glm::vec3 position     = {0.0f, 0.0f, 0.0f}; // feet position (base of capsule), not center
    };
}

#endif //OSIRIS_PHYSICSTYPES_H
