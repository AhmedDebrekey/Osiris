#include "ScriptTemplate.h"

#include <filesystem>
#include <fstream>

#include "core/Log.h"

namespace Osiris {
    namespace {
        // Keep in sync with docs/scripting_api.html's "Auto-generated template" example.
        constexpr const char* kTemplate =
            "function OnStart()\n"
            "end\n"
            "\n"
            "function OnUpdate(dt)\n"
            "end\n"
            "\n"
            "function OnFixedUpdate(fixedDt)\n"
            "end\n";
    }

    void CreateScriptFileIfMissing(const std::string& scriptPath) {
        if (scriptPath.empty() || std::filesystem::exists(scriptPath)) return;

        const std::filesystem::path path(scriptPath);
        if (path.has_parent_path()) {
            std::filesystem::create_directories(path.parent_path());
        }

        std::ofstream file(scriptPath);
        if (!file) {
            OSIRIS_ERROR("CreateScriptFileIfMissing: failed to create '{}'", scriptPath);
            return;
        }
        file << kTemplate;
    }
}
