#include "LuaScripting.h"

#include "core/Log.h"
#include "scene/Scene.h"
#include "scene/Components.h"
#include "physics/IPhysics.h"
#include "audio/IAudio.h"
#include "platform/Input.h"

namespace {
    // Same slot-reuse pattern as OpenALAudio/JoltPhysics, duplicated locally (see OpenALAudio.cpp).
    template<typename T>
    uint32_t AllocateSlot(std::vector<T>& slots, T item, auto isNull) {
        for (uint32_t i = 0; i < slots.size(); i++) {
            if (isNull(slots[i])) {
                slots[i] = std::move(item);
                return i;
            }
        }
        slots.push_back(std::move(item));
        return static_cast<uint32_t>(slots.size() - 1);
    }

    // Same cadence as JoltPhysics::kFixedTimeStep, kept independent per the project's convention.
    constexpr float kFixedTimeStep = 1.0f / 60.0f;
}

namespace Osiris {
    bool LuaScripting::Init(IPhysics* physics, IAudio* audio, Input* input) {
        m_Physics = physics;
        m_Audio   = audio;
        m_Input   = input;

        m_Lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table, sol::lib::os);
        BindAPI();

        m_Lua["physics"] = m_Physics;
        m_Lua["audio"]   = m_Audio;
        m_Lua["input"]   = m_Input;

        OSIRIS_INFO("LuaScripting: initialized (sol2 + Lua)");
        return true;
    }

    void LuaScripting::Shutdown() {
        m_Instances.clear();
    }

    void LuaScripting::BindAPI() {
        m_Lua.new_usertype<glm::vec3>("vec3",
            sol::constructors<glm::vec3(), glm::vec3(float), glm::vec3(float, float, float)>(),
            "x", &glm::vec3::x,
            "y", &glm::vec3::y,
            "z", &glm::vec3::z);

        m_Lua.new_usertype<glm::vec2>("vec2",
            sol::constructors<glm::vec2(), glm::vec2(float), glm::vec2(float, float)>(),
            "x", &glm::vec2::x,
            "y", &glm::vec2::y);

        m_Lua.new_usertype<TransformComponent>("Transform",
            "position",    &TransformComponent::position,
            "rotation",    &TransformComponent::rotation, // euler degrees
            "scale",       &TransformComponent::scale,
            "GetForward",  &TransformComponent::GetForward);

        m_Lua.new_usertype<TagComponent>("Tag",
            "name", &TagComponent::name);

        m_Lua.new_usertype<SpotLightComponent>("SpotLight",
            "color",       &SpotLightComponent::color,
            "intensity",   &SpotLightComponent::intensity,
            "innerCone",   &SpotLightComponent::innerCone,
            "outerCone",   &SpotLightComponent::outerCone,
            "range",       &SpotLightComponent::range,
            "enabled",     &SpotLightComponent::enabled,
            "castsShadow", &SpotLightComponent::castsShadow);

        // Editing halfExtents alone doesn't resize the live Jolt body — use entity:SetColliderHalfExtents.
        m_Lua.new_usertype<ColliderComponent>("Collider",
            "halfExtents", &ColliderComponent::halfExtents);

        m_Lua.new_enum("BodyMotionType",
            "Static",    BodyMotionType::Static,
            "Kinematic", BodyMotionType::Kinematic,
            "Dynamic",   BodyMotionType::Dynamic);

        m_Lua.new_usertype<PhysicsBodyHandle>("PhysicsBodyHandle", "IsValid", &PhysicsBodyHandle::IsValid);
        m_Lua.new_usertype<CharacterHandle>("CharacterHandle", "IsValid", &CharacterHandle::IsValid);
        m_Lua.new_usertype<AudioBufferHandle>("AudioBufferHandle", "IsValid", &AudioBufferHandle::IsValid);
        m_Lua.new_usertype<AudioSourceHandle>("AudioSourceHandle", "IsValid", &AudioSourceHandle::IsValid);

        m_Lua.new_usertype<BoxColliderDesc>("BoxColliderDesc",
            sol::constructors<BoxColliderDesc()>(),
            "halfExtents", &BoxColliderDesc::halfExtents);

        m_Lua.new_usertype<RigidBodyDesc>("RigidBodyDesc",
            sol::constructors<RigidBodyDesc()>(),
            "collider",      &RigidBodyDesc::collider,
            "motionType",    &RigidBodyDesc::motionType,
            "position",      &RigidBodyDesc::position,
            "rotationEuler", &RigidBodyDesc::rotationEuler);

        m_Lua.new_usertype<CharacterDesc>("CharacterDesc",
            sol::constructors<CharacterDesc()>(),
            "radius",          &CharacterDesc::radius,
            "height",          &CharacterDesc::height,
            "maxSlopeAngleDeg", &CharacterDesc::maxSlopeAngleDeg,
            "mass",            &CharacterDesc::mass,
            "position",        &CharacterDesc::position);

        // Changing motionType alone doesn't re-type the live Jolt body — use entity:SetRigidBodyMotionType.
        m_Lua.new_usertype<RigidBodyComponent>("RigidBody",
            "motionType", &RigidBodyComponent::motionType,
            "bodyHandle", &RigidBodyComponent::bodyHandle);

        // Fields don't push to the live OpenAL source on their own — call the matching audio: function.
        m_Lua.new_usertype<AudioSourceComponent>("AudioSource",
            "clip",              &AudioSourceComponent::clip,
            "gain",              &AudioSourceComponent::gain,
            "pitch",             &AudioSourceComponent::pitch,
            "loop",              &AudioSourceComponent::loop,
            "autoPlay",          &AudioSourceComponent::autoPlay,
            "referenceDistance", &AudioSourceComponent::referenceDistance,
            "maxDistance",       &AudioSourceComponent::maxDistance,
            "rolloffFactor",     &AudioSourceComponent::rolloffFactor,
            "sourceHandle",      &AudioSourceComponent::sourceHandle);

        m_Lua.new_usertype<AudioSourceDesc>("AudioSourceDesc",
            sol::constructors<AudioSourceDesc()>(),
            "buffer",            &AudioSourceDesc::buffer,
            "gain",              &AudioSourceDesc::gain,
            "pitch",             &AudioSourceDesc::pitch,
            "loop",              &AudioSourceDesc::loop,
            "referenceDistance", &AudioSourceDesc::referenceDistance,
            "maxDistance",       &AudioSourceDesc::maxDistance,
            "rolloffFactor",     &AudioSourceDesc::rolloffFactor);

        m_Lua.new_usertype<CameraComponent>("Camera",
            "eyeHeight", &CameraComponent::eyeHeight,
            "isPrimary", &CameraComponent::isPrimary);

        // Editing fields alone doesn't recreate the live Jolt character (no rebuild helper bound yet).
        m_Lua.new_usertype<CharacterComponent>("Character",
            "radius",           &CharacterComponent::radius,
            "height",           &CharacterComponent::height,
            "maxSlopeAngleDeg", &CharacterComponent::maxSlopeAngleDeg,
            "mass",             &CharacterComponent::mass,
            "characterHandle",  &CharacterComponent::characterHandle);

        // Each accessor binds a specific template instantiation directly, not a reflection layer.
        m_Lua.new_usertype<Entity>("Entity",
            "IsValid", &Entity::IsValid,

            "GetTransform", &Entity::GetComponent<TransformComponent>,
            "HasTransform", &Entity::HasComponent<TransformComponent>,
            "AddTransform", &Entity::AddComponent<TransformComponent>,

            "GetTag", &Entity::GetComponent<TagComponent>,
            "HasTag", &Entity::HasComponent<TagComponent>,

            "GetSpotLight", &Entity::GetComponent<SpotLightComponent>,
            "HasSpotLight", &Entity::HasComponent<SpotLightComponent>,
            "AddSpotLight", &Entity::AddComponent<SpotLightComponent>,

            "GetCollider", &Entity::GetComponent<ColliderComponent>,
            "HasCollider", &Entity::HasComponent<ColliderComponent>,

            "GetRigidBody", &Entity::GetComponent<RigidBodyComponent>,
            "HasRigidBody", &Entity::HasComponent<RigidBodyComponent>,

            // Field assignment can't trigger a rebuild on its own — these do what
            // Scene::RebuildPhysicsBody does in C++ (set + destroy/recreate) in one call.
            "SetColliderHalfExtents", [this](Entity& entity, glm::vec3 halfExtents) {
                if (!entity.HasComponent<ColliderComponent>()) return;
                entity.GetComponent<ColliderComponent>().halfExtents = halfExtents;
                entity.GetScene()->RebuildPhysicsBody(entity, m_Physics);
            },
            "SetRigidBodyMotionType", [this](Entity& entity, BodyMotionType motionType) {
                if (!entity.HasComponent<RigidBodyComponent>()) return;
                entity.GetComponent<RigidBodyComponent>().motionType = motionType;
                entity.GetScene()->RebuildPhysicsBody(entity, m_Physics);
            },

            "GetAudioSource", &Entity::GetComponent<AudioSourceComponent>,
            "HasAudioSource", &Entity::HasComponent<AudioSourceComponent>,

            "GetCamera", &Entity::GetComponent<CameraComponent>,
            "HasCamera", &Entity::HasComponent<CameraComponent>,
            "AddCamera", &Entity::AddComponent<CameraComponent>,

            // No AddCharacter — needs a Scene::CreateCharacters pass to get a live body.
            "GetCharacter", &Entity::GetComponent<CharacterComponent>,
            "HasCharacter", &Entity::HasComponent<CharacterComponent>);

        // Script-created entities only get Tag+Transform — no mesh/body/source until scene setup.
        m_Lua.new_usertype<Scene>("Scene",
            "FindEntityByName", &Scene::FindEntityByName,
            "CreateEntity",     &Scene::CreateEntity,
            "FindCameraEntity", &Scene::FindCameraEntity);

        m_Lua.new_usertype<IPhysics>("IPhysics",
            "CreateBody",  &IPhysics::CreateBody,
            "DestroyBody", &IPhysics::DestroyBody,
            "GetBodyPosition",     &IPhysics::GetBodyPosition,
            "GetBodyRotationEuler", &IPhysics::GetBodyRotationEuler,
            "CreateCharacter",  &IPhysics::CreateCharacter,
            "DestroyCharacter", &IPhysics::DestroyCharacter,
            "SetCharacterDesiredVelocity", &IPhysics::SetCharacterDesiredVelocity,
            "GetCharacterPosition",        &IPhysics::GetCharacterPosition,
            "IsCharacterGrounded",         &IPhysics::IsCharacterGrounded);

        // CreateBuffer/DestroyBuffer not bound — raw PCM bytes, no legitimate way to construct from Lua.
        m_Lua.new_usertype<IAudio>("IAudio",
            "PlaySound",   &IAudio::PlaySound,
            "CreateSource",  &IAudio::CreateSource,
            "DestroySource", &IAudio::DestroySource,
            "SetSourcePosition", &IAudio::SetSourcePosition,
            "PlaySource",  &IAudio::PlaySource,
            "StopSource",  &IAudio::StopSource,
            "IsSourcePlaying", &IAudio::IsSourcePlaying,
            "SetListenerTransform", &IAudio::SetListenerTransform);

        // Wrapped to take a plain int — Lua has no SDL_Scancode concept; pass a Key table value.
        m_Lua.new_usertype<Input>("Input",
            "IsKeyHeld",    [](Input& input, int key) { return input.IsKeyHeld(static_cast<SDL_Scancode>(key)); },
            "IsKeyPressed", [](Input& input, int key) { return input.IsKeyPressed(static_cast<SDL_Scancode>(key)); },
            "GetMouseDelta",    &Input::GetMouseDelta,
            "GetMousePosition", &Input::GetMousePosition,
            "IsMouseButtonHeld", &Input::IsMouseButtonHeld);

        sol::table keyTable = m_Lua.create_table();
        keyTable["W"] = SDL_SCANCODE_W;
        keyTable["A"] = SDL_SCANCODE_A;
        keyTable["S"] = SDL_SCANCODE_S;
        keyTable["D"] = SDL_SCANCODE_D;
        keyTable["E"] = SDL_SCANCODE_E;
        keyTable["F"] = SDL_SCANCODE_F;
        keyTable["Q"] = SDL_SCANCODE_Q;
        keyTable["R"] = SDL_SCANCODE_R;
        keyTable["Space"] = SDL_SCANCODE_SPACE;
        keyTable["Escape"] = SDL_SCANCODE_ESCAPE;
        keyTable["LeftShift"] = SDL_SCANCODE_LSHIFT;
        keyTable["LeftCtrl"] = SDL_SCANCODE_LCTRL;
        keyTable["Up"] = SDL_SCANCODE_UP;
        keyTable["Down"] = SDL_SCANCODE_DOWN;
        keyTable["Left"] = SDL_SCANCODE_LEFT;
        keyTable["Right"] = SDL_SCANCODE_RIGHT;
        m_Lua["Key"] = keyTable;

        sol::table mouseButtonTable = m_Lua.create_table();
        mouseButtonTable["Left"]   = SDL_BUTTON_LEFT;
        mouseButtonTable["Right"]  = SDL_BUTTON_RIGHT;
        mouseButtonTable["Middle"] = SDL_BUTTON_MIDDLE;
        m_Lua["MouseButton"] = mouseButtonTable;

        // Routed through spdlog so script output doesn't get lost in a windowed build.
        sol::function tostring = m_Lua["tostring"];
        m_Lua.set_function("print", [tostring](sol::variadic_args args) {
            std::string line;
            for (auto arg : args) {
                if (!line.empty()) line += "\t";
                line += tostring(arg).get<std::string>();
            }
            OSIRIS_INFO("[Lua] {}", line);
        });
    }

    ScriptInstanceHandle LuaScripting::CreateInstance(Entity entity, const std::string& scriptPath) {
        if (!entity.IsValid()) {
            OSIRIS_ERROR("LuaScripting: CreateInstance called with an invalid entity");
            return ScriptInstanceHandle{};
        }

        // Own environment per instance so two entities' scripts don't clobber each other's OnUpdate.
        sol::environment env(m_Lua, sol::create, m_Lua.globals());
        env["self"]  = entity;
        env["scene"] = entity.GetScene();

        sol::protected_function_result result = m_Lua.script_file(scriptPath, env, sol::script_pass_on_error);
        if (!result.valid()) {
            sol::error err = result;
            OSIRIS_ERROR("LuaScripting: failed to load '{}': {}", scriptPath, err.what());
            return ScriptInstanceHandle{};
        }

        ScriptInstance instance;
        instance.entity        = entity;
        instance.scriptPath    = scriptPath;
        instance.env           = env;
        instance.onStart       = env["OnStart"];
        instance.onUpdate      = env["OnUpdate"];
        instance.onFixedUpdate = env["OnFixedUpdate"];

        uint32_t index = AllocateSlot(m_Instances, std::move(instance),
            [](const ScriptInstance& s) { return !s.entity.IsValid(); });
        return ScriptInstanceHandle{ index };
    }

    void LuaScripting::DestroyInstance(ScriptInstanceHandle handle) {
        if (!handle.IsValid() || handle.id >= m_Instances.size()) return;
        m_Instances[handle.id] = ScriptInstance{};
    }

    void LuaScripting::Update(float deltaTime) {
        for (auto& instance : m_Instances) {
            if (!instance.entity.IsValid()) continue;

            if (!instance.started) {
                instance.started = true;
                if (instance.onStart.valid()) {
                    sol::protected_function_result result = instance.onStart();
                    if (!result.valid()) {
                        sol::error err = result;
                        OSIRIS_ERROR("LuaScripting: OnStart() error in '{}': {}", instance.scriptPath, err.what());
                    }
                }
            }

            if (instance.onUpdate.valid()) {
                sol::protected_function_result result = instance.onUpdate(deltaTime);
                if (!result.valid()) {
                    sol::error err = result;
                    OSIRIS_ERROR("LuaScripting: OnUpdate() error in '{}': {}", instance.scriptPath, err.what());
                }
            }
        }
    }

    void LuaScripting::FixedUpdate(float deltaTime) {
        m_Accumulator += deltaTime;
        while (m_Accumulator >= kFixedTimeStep) {
            m_Accumulator -= kFixedTimeStep;
            for (auto& instance : m_Instances) {
                if (!instance.entity.IsValid() || !instance.onFixedUpdate.valid()) continue;
                sol::protected_function_result result = instance.onFixedUpdate(kFixedTimeStep);
                if (!result.valid()) {
                    sol::error err = result;
                    OSIRIS_ERROR("LuaScripting: OnFixedUpdate() error in '{}': {}", instance.scriptPath, err.what());
                }
            }
        }
    }
}
