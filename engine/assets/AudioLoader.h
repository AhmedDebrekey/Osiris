#ifndef OSIRIS_AUDIOLOADER_H
#define OSIRIS_AUDIOLOADER_H

#include <string>

#include "audio/AudioTypes.h"

namespace Osiris {
    class AudioLoader {
    public:
        // Uncompressed PCM WAV only for now (audioFormat == 1) — OGG (via stb_vorbis) is a
        // separate follow-up checkpoint. Returns an empty PCMAudioData (pcmData.empty()) on
        // any failure; check that before handing the result to IAudio::CreateBuffer.
        static PCMAudioData LoadWAV(const std::string& path);
    };
}

#endif //OSIRIS_AUDIOLOADER_H
