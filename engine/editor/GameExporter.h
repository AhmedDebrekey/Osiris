#ifndef OSIRIS_GAMEEXPORTER_H
#define OSIRIS_GAMEEXPORTER_H

#include <cstdint>
#include <string>

namespace Osiris {
    class Scene;

    struct GameExportResult {
        bool succeeded = false;
        std::string message;
    };

    class GameExporter {
    public:
        static GameExportResult Export(Scene& scene, const std::string& gameName,
            uint32_t maxFps, bool showFps);
    };
}

#endif //OSIRIS_GAMEEXPORTER_H
