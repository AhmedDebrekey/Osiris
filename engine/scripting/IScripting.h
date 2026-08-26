#ifndef OSIRIS_ISCRIPTING_H
#define OSIRIS_ISCRIPTING_H

#include <string>

#include "ScriptTypes.h"

namespace Osiris {
    class Entity;
    class IRHI;
    class IPhysics;
    class IAudio;
    class Input;

    // Mirrors IPhysics/IAudio — no raw sol2/lua_State types outside the backend.
    class IScripting {
    public:
        virtual ~IScripting() = default;

        // Physics/audio/input are handed to every script instance as globals — see LuaScripting::BindAPI.
        // rhi isn't a Lua global itself (too broad a surface); it only backs Entity mesh-creation helpers.
        virtual bool Init(IRHI* rhi, IPhysics* physics, IAudio* audio, Input* input) = 0;
        virtual void Shutdown() = 0;

        // Sandboxes scriptPath into an environment bound to entity (exposed as `self`/`scene`).
        virtual ScriptInstanceHandle CreateInstance(Entity entity, const std::string& scriptPath) = 0;
        virtual void DestroyInstance(ScriptInstanceHandle handle) = 0;
        virtual void DispatchCollision(ScriptInstanceHandle handle, Entity otherEntity) = 0;
        virtual void DispatchCollisionEnd(ScriptInstanceHandle handle, Entity otherEntity) = 0;
        virtual void DispatchInteract(ScriptInstanceHandle handle, Entity interactor) = 0;

        // Runs OnStart() once (lazily) then OnUpdate(dt) — call once per rendered frame.
        virtual void Update(float deltaTime) = 0;

        // Runs OnFixedUpdate(fixedDt) on its own fixed-timestep accumulator.
        virtual void FixedUpdate(float deltaTime) = 0;
    };
}

#endif //OSIRIS_ISCRIPTING_H
