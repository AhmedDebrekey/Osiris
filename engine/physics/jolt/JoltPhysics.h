#ifndef OSIRIS_JOLTPHYSICS_H
#define OSIRIS_JOLTPHYSICS_H

#include "physics/IPhysics.h"

#include <memory>
#include <vector>

// The Jolt headers don't include Jolt.h themselves — Jolt.h must always come first.
#include <Jolt/Jolt.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>

namespace Osiris {
    // Jolt-backed implementation of IPhysics. All Jolt types stay confined to this class —
    // IPhysics.h itself never includes a Jolt header, matching how VulkanRHI is the only
    // place raw Vulkan calls are allowed.
    class JoltPhysics final : public IPhysics {
    public:
        bool Init() override;
        void Shutdown() override;
        void Update(float deltaTime) override;

        PhysicsBodyHandle CreateBody(const RigidBodyDesc& desc) override;
        void DestroyBody(PhysicsBodyHandle handle) override;
        void ApplyImpulse(PhysicsBodyHandle handle, const glm::vec3& impulse) override;
        void SetBodyVelocity(PhysicsBodyHandle handle, const glm::vec3& linearVelocity,
                             const glm::vec3& angularVelocity) override;
        glm::vec3 GetBodyPosition(PhysicsBodyHandle handle) const override;
        glm::vec3 GetBodyRotationEuler(PhysicsBodyHandle handle) const override;

        CharacterHandle CreateCharacter(const CharacterDesc& desc) override;
        void DestroyCharacter(CharacterHandle handle) override;

        void SetCharacterDesiredVelocity(CharacterHandle handle, const glm::vec3& horizontalVelocity, bool jump) override;
        glm::vec3 GetCharacterPosition(CharacterHandle handle) const override;
        bool IsCharacterGrounded(CharacterHandle handle) const override;

    private:
        // Two object layers is enough for static world geometry + moving characters/bodies.
        // Add more only once a concrete case needs finer-grained collision rules.
        struct Layers {
            static constexpr JPH::ObjectLayer NON_MOVING = 0;
            static constexpr JPH::ObjectLayer MOVING     = 1;
            static constexpr JPH::ObjectLayer NUM_LAYERS = 2;
        };
        struct BroadPhaseLayers {
            static constexpr JPH::BroadPhaseLayer NON_MOVING{0};
            static constexpr JPH::BroadPhaseLayer MOVING{1};
            static constexpr JPH::uint NUM_LAYERS = 2;
        };

        class BPLayerInterface final : public JPH::BroadPhaseLayerInterface {
        public:
            BPLayerInterface();
            JPH::uint GetNumBroadPhaseLayers() const override;
            JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override;
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
            const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override;
#endif
        private:
            JPH::BroadPhaseLayer m_ObjectToBroadPhase[Layers::NUM_LAYERS];
        };

        class ObjectVsBroadPhaseLayerFilterImpl final : public JPH::ObjectVsBroadPhaseLayerFilter {
        public:
            bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const override;
        };

        class ObjectLayerPairFilterImpl final : public JPH::ObjectLayerPairFilter {
        public:
            bool ShouldCollide(JPH::ObjectLayer inObject1, JPH::ObjectLayer inObject2) const override;
        };

        void StepCharacters(float fixedDeltaTime);

        static constexpr float kFixedTimeStep = 1.0f / 60.0f;
        static constexpr float kJumpSpeed     = 4.0f;

        BPLayerInterface              m_BroadPhaseLayerInterface;
        ObjectVsBroadPhaseLayerFilterImpl m_ObjectVsBroadPhaseLayerFilter;
        ObjectLayerPairFilterImpl     m_ObjectLayerPairFilter;

        JPH::PhysicsSystem m_PhysicsSystem;
        std::unique_ptr<JPH::TempAllocatorImpl>   m_TempAllocator;
        std::unique_ptr<JPH::JobSystemThreadPool> m_JobSystem;

        float m_Accumulator = 0.0f;

        std::vector<JPH::BodyID> m_Bodies;

        struct CharacterEntry {
            JPH::Ref<JPH::CharacterVirtual> character;
            glm::vec3 desiredHorizontalVelocity{0.0f};
            bool jumpRequested = false;
            // Position before/after the most recent fixed step, so GetCharacterPosition() can
            // interpolate by the leftover accumulator fraction instead of returning the raw
            // 60Hz-stepped position — without this, camera motion visibly jitters because the
            // render rate isn't phase-locked to the fixed physics rate.
            glm::vec3 previousPosition{0.0f};
            glm::vec3 currentPosition{0.0f};
        };
        std::vector<CharacterEntry> m_Characters;
    };
}

#endif //OSIRIS_JOLTPHYSICS_H
