#ifndef OSIRIS_SCRIPTTEMPLATE_H
#define OSIRIS_SCRIPTTEMPLATE_H

#include <string>

namespace Osiris {
    // Writes a fresh OnStart/OnUpdate/OnFixedUpdate stub to scriptPath if no file exists there
    // yet — called whenever a ScriptComponent is set up, so an entity always has something
    // loadable instead of erroring on a missing file. Never overwrites an existing script.
    void CreateScriptFileIfMissing(const std::string& scriptPath);
}

#endif //OSIRIS_SCRIPTTEMPLATE_H
