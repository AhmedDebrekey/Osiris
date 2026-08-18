#ifndef OSIRIS_IAUDIO_H
#define OSIRIS_IAUDIO_H

#include "AudioTypes.h"

namespace Osiris {
    // Backend-agnostic audio interface, mirroring IRHI/IPhysics: engine/scene and games/testbed
    // only ever talk to IAudio, never to OpenAL types directly.
    class IAudio {
    public:
        virtual ~IAudio() = default;

        virtual bool Init() = 0;
        virtual void Shutdown() = 0;

        virtual AudioBufferHandle CreateBuffer(const PCMAudioData& data) = 0;
        virtual void DestroyBuffer(AudioBufferHandle handle) = 0;

        // Non-positional, fire-and-forget playback (reuses a single internal source) — just
        // enough to confirm a loaded buffer actually makes sound. Kept around from checkpoint 1
        // for one-shot UI-style sounds later; gameplay emitters should use CreateSource instead.
        virtual void PlaySound(AudioBufferHandle handle) = 0;

        // Persistent 3D positional sources, driven by Scene::CreateAudioSources/SyncAudioSources
        // from AudioSourceComponent — position is world-space and expected to be updated every
        // frame via SetSourcePosition (cheap no-op in OpenAL if unchanged).
        virtual AudioSourceHandle CreateSource(const AudioSourceDesc& desc) = 0;
        virtual void DestroySource(AudioSourceHandle handle) = 0;
        virtual void SetSourcePosition(AudioSourceHandle handle, const glm::vec3& position) = 0;
        virtual void PlaySource(AudioSourceHandle handle) = 0;
        virtual void StopSource(AudioSourceHandle handle) = 0;
        virtual bool IsSourcePlaying(AudioSourceHandle handle) const = 0;

        // The "ears" OpenAL computes panning/attenuation relative to — call once per frame with
        // whichever camera is currently driving the view.
        virtual void SetListenerTransform(const glm::vec3& position, const glm::vec3& forward, const glm::vec3& up) = 0;
    };
}

#endif //OSIRIS_IAUDIO_H
