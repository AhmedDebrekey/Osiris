#ifndef OSIRIS_OPENALAUDIO_H
#define OSIRIS_OPENALAUDIO_H

#include "audio/IAudio.h"

#include <vector>

#include <AL/al.h>
#include <AL/alc.h>

namespace Osiris {
    // OpenAL Soft-backed implementation of IAudio. All OpenAL types stay confined to this
    // class — IAudio.h itself never includes an AL header, matching how VulkanRHI/JoltPhysics
    // are the only places raw Vulkan/Jolt calls are allowed.
    class OpenALAudio final : public IAudio {
    public:
        bool Init() override;
        void Shutdown() override;

        AudioBufferHandle CreateBuffer(const PCMAudioData& data) override;
        void DestroyBuffer(AudioBufferHandle handle) override;

        void PlaySound(AudioBufferHandle handle) override;

        AudioSourceHandle CreateSource(const AudioSourceDesc& desc) override;
        void DestroySource(AudioSourceHandle handle) override;
        void SetSourcePosition(AudioSourceHandle handle, const glm::vec3& position) override;
        void PlaySource(AudioSourceHandle handle) override;
        void StopSource(AudioSourceHandle handle) override;
        bool IsSourcePlaying(AudioSourceHandle handle) const override;

        void SetListenerTransform(const glm::vec3& position, const glm::vec3& forward, const glm::vec3& up) override;

    private:
        ALCdevice*  m_Device  = nullptr;
        ALCcontext* m_Context = nullptr;

        std::vector<ALuint> m_Buffers;
        std::vector<ALuint> m_Sources; // persistent 3D sources, created via CreateSource

        // Reused for every PlaySound() call — one-shot non-positional playback doesn't need
        // per-call source allocation.
        ALuint m_TestSource = 0;
    };
}

#endif //OSIRIS_OPENALAUDIO_H
