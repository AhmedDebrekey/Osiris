#include "JoltPhysics.h"

#include <algorithm>
#include <cmath>
#include <thread>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Geometry/Plane.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>

#include "core/Log.h"

namespace {
    // Same slot-reuse pattern as VulkanRHI's AllocateSlot (engine/rhi_vulkan/VulkanRHI.cpp) —
    // duplicated locally rather than shared since it's a five-line helper and physics/rhi
    // have no other reason to depend on each other.
    template<typename T>
    uint32_t AllocateSlot(std::vector<T>& slots, const T& item, auto isNull) {
        for (uint32_t i = 0; i < slots.size(); i++) {
            if (isNull(slots[i])) {
                slots[i] = item;
                return i;
            }
        }
        slots.push_back(item);
        return static_cast<uint32_t>(slots.size() - 1);
    }

    glm::vec3 ToGlm(JPH::RVec3Arg v) {
        return glm::vec3(v.GetX(), v.GetY(), v.GetZ());
    }

    // Real crates/props aren't solid blocks — ConvexShape's default density (1000 kg/m^3, i.e.
    // water) makes a ~0.7m box weigh ~340kg, which CharacterVirtual's default 100N push
    // strength can technically move but far too slowly to look like anything is happening.
    constexpr float kDynamicBodyDensity = 200.0f;

    // Mirrors TransformComponent::GetModelMatrix's rotation order (Rx * Ry * Rz) exactly,
    // so a collider's rotationEuler lines up with how the same entity would be rendered.
    JPH::Quat ToJoltQuat(const glm::vec3& eulerDegrees) {
        glm::mat4 rot(1.0f);
        rot = glm::rotate(rot, glm::radians(eulerDegrees.x), glm::vec3(1.0f, 0.0f, 0.0f));
        rot = glm::rotate(rot, glm::radians(eulerDegrees.y), glm::vec3(0.0f, 1.0f, 0.0f));
        rot = glm::rotate(rot, glm::radians(eulerDegrees.z), glm::vec3(0.0f, 0.0f, 1.0f));
        glm::quat q = glm::quat_cast(rot);
        return JPH::Quat(q.x, q.y, q.z, q.w);
    }
}

namespace Osiris {
    // ── BPLayerInterface ─────────────────────────────────────────────
    JoltPhysics::BPLayerInterface::BPLayerInterface() {
        m_ObjectToBroadPhase[Layers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
        m_ObjectToBroadPhase[Layers::MOVING]     = BroadPhaseLayers::MOVING;
    }

    JPH::uint JoltPhysics::BPLayerInterface::GetNumBroadPhaseLayers() const {
        return BroadPhaseLayers::NUM_LAYERS;
    }

    JPH::BroadPhaseLayer JoltPhysics::BPLayerInterface::GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const {
        return m_ObjectToBroadPhase[inLayer];
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* JoltPhysics::BPLayerInterface::GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const {
        switch ((JPH::BroadPhaseLayer::Type)inLayer) {
            case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::NON_MOVING: return "NON_MOVING";
            case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::MOVING:     return "MOVING";
            default:                                                      return "INVALID";
        }
    }
#endif

    // ── ObjectVsBroadPhaseLayerFilterImpl ────────────────────────────
    bool JoltPhysics::ObjectVsBroadPhaseLayerFilterImpl::ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const {
        switch (inLayer1) {
            case Layers::NON_MOVING: return inLayer2 == BroadPhaseLayers::MOVING;
            case Layers::MOVING:     return true;
            default:                 return false;
        }
    }

    // ── ObjectLayerPairFilterImpl ─────────────────────────────────────
    bool JoltPhysics::ObjectLayerPairFilterImpl::ShouldCollide(JPH::ObjectLayer inObject1, JPH::ObjectLayer inObject2) const {
        switch (inObject1) {
            case Layers::NON_MOVING: return inObject2 == Layers::MOVING;
            case Layers::MOVING:     return true;
            default:                 return false;
        }
    }

    // ── Lifecycle ──────────────────────────────────────────────────
    bool JoltPhysics::Init() {
        JPH::RegisterDefaultAllocator();
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();

        m_TempAllocator = std::make_unique<JPH::TempAllocatorImpl>(10 * 1024 * 1024);

        const unsigned int numThreads = std::max(1u, std::thread::hardware_concurrency() - 1);
        m_JobSystem = std::make_unique<JPH::JobSystemThreadPool>(
            JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, numThreads);

        constexpr JPH::uint kMaxBodies            = 4096;
        constexpr JPH::uint kNumBodyMutexes        = 0; // 0 = Jolt picks a default
        constexpr JPH::uint kMaxBodyPairs          = 4096;
        constexpr JPH::uint kMaxContactConstraints = 4096;

        m_PhysicsSystem.Init(kMaxBodies, kNumBodyMutexes, kMaxBodyPairs, kMaxContactConstraints,
            m_BroadPhaseLayerInterface, m_ObjectVsBroadPhaseLayerFilter, m_ObjectLayerPairFilter);

        OSIRIS_INFO("JoltPhysics: initialized ({} worker threads)", numThreads);
        return true;
    }

    void JoltPhysics::Shutdown() {
        JPH::BodyInterface& bodyInterface = m_PhysicsSystem.GetBodyInterface();
        for (auto& id : m_Bodies) {
            if (!id.IsInvalid()) {
                bodyInterface.RemoveBody(id);
                bodyInterface.DestroyBody(id);
            }
        }
        m_Bodies.clear();
        m_Characters.clear(); // releases each CharacterVirtual via JPH::Ref

        m_JobSystem.reset();
        m_TempAllocator.reset();

        JPH::UnregisterTypes();
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;
    }

    void JoltPhysics::Update(float deltaTime) {
        m_Accumulator += deltaTime;
        // Clamp so a huge deltaTime (window drag, breakpoint) can't spiral the accumulator
        // into an ever-growing catch-up loop — better to lose time than hang the frame.
        m_Accumulator = std::min(m_Accumulator, kFixedTimeStep * 8.0f);

        while (m_Accumulator >= kFixedTimeStep) {
            StepCharacters(kFixedTimeStep);
            m_PhysicsSystem.Update(kFixedTimeStep, 1, m_TempAllocator.get(), m_JobSystem.get());
            m_Accumulator -= kFixedTimeStep;
        }
    }

    void JoltPhysics::StepCharacters(float fixedDeltaTime) {
        for (auto& entry : m_Characters) {
            if (entry.character == nullptr) continue;
            JPH::CharacterVirtual* character = entry.character;

            // Ground/gravity/jump integration, following Jolt's recommended
            // CharacterVirtualTest pattern (assume-ground-velocity-when-supported).
            JPH::Vec3 currentVerticalVelocity = character->GetLinearVelocity().Dot(character->GetUp()) * character->GetUp();
            JPH::Vec3 groundVelocity = character->GetGroundVelocity();
            JPH::Vec3 newVelocity;

            bool movingTowardsGround = (currentVerticalVelocity.GetY() - groundVelocity.GetY()) < 0.1f;
            if (character->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround && movingTowardsGround) {
                newVelocity = groundVelocity;
                if (entry.jumpRequested)
                    newVelocity += JPH::Vec3(0.0f, kJumpSpeed, 0.0f);
            } else {
                newVelocity = currentVerticalVelocity;
            }
            entry.jumpRequested = false;

            newVelocity += m_PhysicsSystem.GetGravity() * fixedDeltaTime;
            newVelocity += JPH::Vec3(entry.desiredHorizontalVelocity.x, 0.0f, entry.desiredHorizontalVelocity.z);

            character->SetLinearVelocity(newVelocity);

            entry.previousPosition = entry.currentPosition;

            JPH::CharacterVirtual::ExtendedUpdateSettings updateSettings;
            character->ExtendedUpdate(fixedDeltaTime,
                -character->GetUp() * m_PhysicsSystem.GetGravity().Length(),
                updateSettings,
                m_PhysicsSystem.GetDefaultBroadPhaseLayerFilter(Layers::MOVING),
                m_PhysicsSystem.GetDefaultLayerFilter(Layers::MOVING),
                {},
                {},
                *m_TempAllocator);

            entry.currentPosition = ToGlm(character->GetPosition());
        }
    }

    // ── Static/dynamic bodies ─────────────────────────────────────────
    PhysicsBodyHandle JoltPhysics::CreateBody(const RigidBodyDesc& desc) {
        JPH::EMotionType motionType = JPH::EMotionType::Static;
        JPH::ObjectLayer layer = Layers::NON_MOVING;
        if (desc.motionType == BodyMotionType::Dynamic) {
            motionType = JPH::EMotionType::Dynamic;
            layer = Layers::MOVING;
        } else if (desc.motionType == BodyMotionType::Kinematic) {
            motionType = JPH::EMotionType::Kinematic;
            layer = Layers::MOVING;
        }

        JPH::Vec3 halfExtents(desc.collider.halfExtents.x, desc.collider.halfExtents.y, desc.collider.halfExtents.z);
        JPH::RVec3 position(desc.position.x, desc.position.y, desc.position.z);
        JPH::Quat rotation = ToJoltQuat(desc.rotationEuler);

        // Mass only matters for Dynamic bodies, but setting it unconditionally is harmless
        // (static/kinematic bodies never use mass) and keeps this branch-free.
        JPH::BoxShape* boxShape = new JPH::BoxShape(halfExtents);
        boxShape->SetDensity(kDynamicBodyDensity);

        JPH::BodyCreationSettings settings(boxShape, position, rotation, motionType, layer);

        JPH::EActivation activation = motionType == JPH::EMotionType::Static
            ? JPH::EActivation::DontActivate
            : JPH::EActivation::Activate;
        JPH::BodyID id = m_PhysicsSystem.GetBodyInterface().CreateAndAddBody(settings, activation);

        uint32_t index = AllocateSlot(m_Bodies, id, [](const JPH::BodyID& b) { return b.IsInvalid(); });
        return PhysicsBodyHandle{ index };
    }

    void JoltPhysics::DestroyBody(PhysicsBodyHandle handle) {
        if (!handle.IsValid() || handle.id >= m_Bodies.size()) return;
        JPH::BodyID& id = m_Bodies[handle.id];
        if (id.IsInvalid()) return;

        JPH::BodyInterface& bodyInterface = m_PhysicsSystem.GetBodyInterface();
        bodyInterface.RemoveBody(id);
        bodyInterface.DestroyBody(id);
        id = JPH::BodyID(); // reset to invalid so AllocateSlot can reuse this slot
    }

    glm::vec3 JoltPhysics::GetBodyPosition(PhysicsBodyHandle handle) const {
        if (!handle.IsValid() || handle.id >= m_Bodies.size() || m_Bodies[handle.id].IsInvalid())
            return glm::vec3(0.0f);
        return ToGlm(m_PhysicsSystem.GetBodyInterface().GetPosition(m_Bodies[handle.id]));
    }

    glm::vec3 JoltPhysics::GetBodyRotationEuler(PhysicsBodyHandle handle) const {
        if (!handle.IsValid() || handle.id >= m_Bodies.size() || m_Bodies[handle.id].IsInvalid())
            return glm::vec3(0.0f);

        JPH::Quat q = m_PhysicsSystem.GetBodyInterface().GetRotation(m_Bodies[handle.id]);
        glm::quat glmQuat(q.GetW(), q.GetX(), q.GetY(), q.GetZ());
        glm::mat3 m = glm::mat3_cast(glmQuat);

        // Decomposition hand-derived to match TransformComponent::GetModelMatrix's Rx*Ry*Rz
        // composition order exactly (Rx(x)*Ry(y)*Rz(z), verified against GLM's own per-axis
        // eulerAngleX/Y/Z matrices rather than assumed): for that composition,
        //   m[2][0] = -sin(y), m[2][1] = -sin(x)cos(y), m[2][2] = cos(x)cos(y),
        //   m[0][0] = cos(y)cos(z), m[1][0] = -cos(y)sin(z).
        // Gimbal lock (y near +-90 deg) makes x/z individually unstable here, same as any
        // Euler-angle representation — acceptable for physics-test visualization.
        float ry = std::asin(std::clamp(-m[2][0], -1.0f, 1.0f));
        float rx = std::atan2(-m[2][1], m[2][2]);
        float rz = std::atan2(-m[1][0], m[0][0]);
        return glm::degrees(glm::vec3(rx, ry, rz));
    }

    // ── Character controller ──────────────────────────────────────────
    CharacterHandle JoltPhysics::CreateCharacter(const CharacterDesc& desc) {
        // desc.position is the character's feet — CharacterVirtual::GetPosition() tracks the
        // shape origin, so the capsule is offset upward by half its height + radius.
        JPH::RefConst<JPH::Shape> shape = new JPH::RotatedTranslatedShape(
            JPH::Vec3(0.0f, 0.5f * desc.height + desc.radius, 0.0f),
            JPH::Quat::sIdentity(),
            new JPH::CapsuleShape(0.5f * desc.height, desc.radius));

        JPH::Ref<JPH::CharacterVirtualSettings> settings = new JPH::CharacterVirtualSettings();
        settings->mShape = shape;
        settings->mMaxSlopeAngle = glm::radians(desc.maxSlopeAngleDeg);
        settings->mMass = desc.mass;
        settings->mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -desc.radius);

        JPH::Ref<JPH::CharacterVirtual> character = new JPH::CharacterVirtual(
            settings,
            JPH::RVec3(desc.position.x, desc.position.y, desc.position.z),
            JPH::Quat::sIdentity(),
            &m_PhysicsSystem);

        CharacterEntry entry;
        entry.character = character;
        entry.previousPosition = desc.position;
        entry.currentPosition  = desc.position;
        uint32_t index = AllocateSlot(m_Characters, entry, [](const CharacterEntry& e) { return e.character == nullptr; });
        return CharacterHandle{ index };
    }

    void JoltPhysics::DestroyCharacter(CharacterHandle handle) {
        if (!handle.IsValid() || handle.id >= m_Characters.size()) return;
        m_Characters[handle.id].character = nullptr;
    }

    void JoltPhysics::SetCharacterDesiredVelocity(CharacterHandle handle, const glm::vec3& horizontalVelocity, bool jump) {
        if (!handle.IsValid() || handle.id >= m_Characters.size()) return;
        auto& entry = m_Characters[handle.id];
        entry.desiredHorizontalVelocity = horizontalVelocity;
        entry.jumpRequested = entry.jumpRequested || jump; // latched until consumed by StepCharacters
    }

    glm::vec3 JoltPhysics::GetCharacterPosition(CharacterHandle handle) const {
        if (!handle.IsValid() || handle.id >= m_Characters.size() || m_Characters[handle.id].character == nullptr)
            return glm::vec3(0.0f);
        // Interpolate by the leftover accumulator fraction rather than returning the raw
        // 60Hz-stepped position — Update() always leaves m_Accumulator in [0, kFixedTimeStep)
        // by the time this is called, so alpha is naturally in [0, 1).
        const auto& entry = m_Characters[handle.id];
        float alpha = m_Accumulator / kFixedTimeStep;
        return glm::mix(entry.previousPosition, entry.currentPosition, alpha);
    }

    bool JoltPhysics::IsCharacterGrounded(CharacterHandle handle) const {
        if (!handle.IsValid() || handle.id >= m_Characters.size() || m_Characters[handle.id].character == nullptr)
            return false;
        return m_Characters[handle.id].character->IsSupported();
    }
}
