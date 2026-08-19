#ifndef OSIRIS_SCRIPTTEMPLATE_H
#define OSIRIS_SCRIPTTEMPLATE_H

#include <string>

namespace Osiris {
    // Writes a fresh OnStart/OnUpdate/OnFixedUpdate stub if scriptPath doesn't exist yet.
    void CreateScriptFileIfMissing(const std::string& scriptPath);
}

#endif //OSIRIS_SCRIPTTEMPLATE_H
