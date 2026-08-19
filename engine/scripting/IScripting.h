#ifndef OSIRIS_ISCRIPTING_H
#define OSIRIS_ISCRIPTING_H

#include <string>

#include "ScriptTypes.h"

namespace Osiris {
    class Entity;
    class IPhysics;
    class IAudio;
    class Input;

    // Backend-agnostic scripting interface, mirroring IPhysics/IAudio: engine/scene and
    // games/testbed only ever talk to IScripting, never to sol2/lua_State directly.
    class IScripting {
    public:
        virtual ~IScripting() = default;

        // Unlike IPhysics/IAudio's parameterless Init(), scripting is a higher-level integration
        // point that needs to hand these three systems to every script instance as globals
        // (physics/audio/input) — see LuaScripting::BindAPI. All three are guaranteed to already
        // exist by the time Engine::Initialize() constructs the scripting backend.
        virtual bool Init(IPhysics* physics, IAudio* audio, Input* input) = 0;
        virtual void Shutdown() = 0;

        // Loads scriptPath into its own sandboxed environment bound to entity (exposed to the
        // script as globals `self` and `scene`) and registers it for per-frame updates.
        // OnStart() runs once, the first time Update() runs for this instance.
        virtual ScriptInstanceHandle CreateInstance(Entity entity, const std::string& scriptPath) = 0;
        virtual void DestroyInstance(ScriptInstanceHandle handle) = 0;

        // Calls each instance's OnStart() (once, lazily) then OnUpdate(dt) — call once per
        // rendered frame with that frame's (variable) deltaTime.
        virtual void Update(float deltaTime) = 0;

        // Calls each instance's OnFixedUpdate(fixedDt) on its own fixed internal timestep
        // (accumulator pattern, same cadence as IPhysics::Update) — call once per rendered
        // frame, independently of Update() above.
        virtual void FixedUpdate(float deltaTime) = 0;
    };
}

#endif //OSIRIS_ISCRIPTING_H
