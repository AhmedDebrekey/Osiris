#ifndef OSIRIS_AUDIOTYPES_H
#define OSIRIS_AUDIOTYPES_H

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "rhi/RHITypes.h" // reuse the generic Handle<Tag> template, same as PhysicsTypes.h does

namespace Osiris {
    struct AudioBufferTag {};
    using AudioBufferHandle = Handle<AudioBufferTag>;

    struct AudioSourceTag {};
    using AudioSourceHandle = Handle<AudioSourceTag>;

    // Raw decoded PCM audio — file-format-agnostic, same split as HDRImageData/IRHI::LoadEnvironmentMap:
    // AudioLoader knows about WAV/OGG file formats, IAudio only ever sees PCM.
    struct PCMAudioData {
        uint32_t sampleRate    = 0;
        uint16_t channels      = 0;
        uint16_t bitsPerSample = 0;
        std::vector<uint8_t> pcmData;
    };

    // Playback parameters for a 3D positional source. OpenALAudio sets the AL_LINEAR_DISTANCE_CLAMPED
    // distance model (not OpenAL's default inverse-distance model, whose "max distance" only
    // caps how far attenuation keeps *increasing* rather than reaching silence — see Init()'s
    // comment): gain is 1.0 inside referenceDistance, falls off linearly, and reaches exactly 0
    // (true silence) at maxDistance and beyond. rolloffFactor scales how steep that falloff is.
    struct AudioSourceDesc {
        AudioBufferHandle buffer;
        float gain              = 1.0f;
        float pitch             = 1.0f;
        bool  loop               = false;
        float referenceDistance = 1.0f;
        float maxDistance       = 30.0f;
        float rolloffFactor     = 1.0f;
    };
}

#endif //OSIRIS_AUDIOTYPES_H
