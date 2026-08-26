#ifndef OSIRIS_LUASCRIPTING_H
#define OSIRIS_LUASCRIPTING_H

#include <deque>
#include <string>

#include <sol/sol.hpp>

#include "scripting/IScripting.h"
#include "scene/Entity.h"

namespace Osiris {
    class LuaScripting : public IScripting {
    public:
        bool Init(IRHI* rhi, IPhysics* physics, IAudio* audio, Input* input) override;
        void Shutdown() override;

        ScriptInstanceHandle CreateInstance(Entity entity, const std::string& scriptPath) override;
        void DestroyInstance(ScriptInstanceHandle handle) override;
        void DispatchCollision(ScriptInstanceHandle handle, Entity otherEntity) override;
        void DispatchCollisionEnd(ScriptInstanceHandle handle, Entity otherEntity) override;
        void DispatchInteract(ScriptInstanceHandle handle, Entity interactor) override;

        void Update(float deltaTime) override;
        void FixedUpdate(float deltaTime) override;

    private:
        void BindAPI();

        struct ScriptInstance {
            Entity entity; // default-constructed (invalid) Entity marks a free slot
            std::string scriptPath;
            sol::environment env;
            sol::protected_function onStart;
            sol::protected_function onUpdate;
            sol::protected_function onFixedUpdate;
            sol::protected_function onCollision;
            sol::protected_function onCollisionEnd;
            sol::protected_function onInteract;
            bool started = false;
        };

        sol::state m_Lua;

        // deque, not vector: Entity:AddScript() (bound in BindAPI) can call CreateInstance()
        // reentrantly from inside the very Update() loop below that iterates m_Instances (a
        // script spawning another scripted entity from its own OnUpdate). A vector's push_back
        // can reallocate and invalidate every reference into it, including the one Update()'s
        // loop is currently holding; deque never invalidates references to existing elements on
        // push_back, only iterators — which is why that loop is index-based, not range-based.
        std::deque<ScriptInstance> m_Instances;
        float m_Accumulator = 0.0f;

        // Engine-wide singletons, bound as true Lua globals in Init() — see BindAPI().
        IRHI*     m_RHI     = nullptr;
        IPhysics* m_Physics = nullptr;
        IAudio*   m_Audio   = nullptr;
        Input*    m_Input   = nullptr;
    };
}

#endif //OSIRIS_LUASCRIPTING_H
