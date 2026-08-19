#ifndef OSIRIS_LUASCRIPTING_H
#define OSIRIS_LUASCRIPTING_H

#include <string>
#include <vector>

#include <sol/sol.hpp>

#include "scripting/IScripting.h"
#include "scene/Entity.h"

namespace Osiris {
    class LuaScripting : public IScripting {
    public:
        bool Init(IPhysics* physics, IAudio* audio, Input* input) override;
        void Shutdown() override;

        ScriptInstanceHandle CreateInstance(Entity entity, const std::string& scriptPath) override;
        void DestroyInstance(ScriptInstanceHandle handle) override;

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
            bool started = false;
        };

        sol::state m_Lua;
        std::vector<ScriptInstance> m_Instances;
        float m_Accumulator = 0.0f;

        // Engine-wide singletons, bound as true Lua globals in Init() — see BindAPI().
        IPhysics* m_Physics = nullptr;
        IAudio*   m_Audio   = nullptr;
        Input*    m_Input   = nullptr;
    };
}

#endif //OSIRIS_LUASCRIPTING_H
