#include "OpenALAudio.h"

#include "core/Log.h"

namespace {
    // Same slot-reuse pattern as VulkanRHI's AllocateSlot / JoltPhysics' local copy — duplicated
    // locally rather than shared since it's a five-line helper (see JoltPhysics.cpp's comment).
    template<typename T>
    uint32_t AllocateSlot(std::vector<T>& slots, const T& item, auto isNull) {
        for (uint32_t i = 0; i < slots.size(); i++) {
            if (isNull(slots[i])) {
                slots[i] = item;
                return i;
            }
        }
        slots.push_back(item);
        return static_cast<uint32_t>(slots.size() - 1);
    }

    bool CheckALError(const char* context) {
        ALenum error = alGetError();
        if (error != AL_NO_ERROR) {
            OSIRIS_ERROR("OpenALAudio: AL error after {}: {}", context, error);
            return false;
        }
        return true;
    }

    ALenum PCMFormat(uint16_t channels, uint16_t bitsPerSample) {
        if (channels == 1) return bitsPerSample == 8 ? AL_FORMAT_MONO8   : AL_FORMAT_MONO16;
        return                     bitsPerSample == 8 ? AL_FORMAT_STEREO8 : AL_FORMAT_STEREO16;
    }

    // OpenAL only spatializes mono sources — a stereo buffer plays back as flat, unattenuated
    // background audio no matter what AL_POSITION/AL_REFERENCE_DISTANCE/etc. are set to. Every
    // buffer this engine creates is meant to end up on a 3D source sooner or later, so downmix
    // unconditionally here rather than relying on every call site to remember it. (If a genuine
    // non-positional stereo use case — e.g. a music bus — shows up later, that's the point to
    // add an opt-out, not before.)
    Osiris::PCMAudioData DownmixToMono(const Osiris::PCMAudioData& stereo) {
        Osiris::PCMAudioData mono;
        mono.sampleRate    = stereo.sampleRate;
        mono.channels      = 1;
        mono.bitsPerSample = stereo.bitsPerSample;

        if (stereo.bitsPerSample == 16) {
            const int16_t* src = reinterpret_cast<const int16_t*>(stereo.pcmData.data());
            size_t frames = stereo.pcmData.size() / (2 * sizeof(int16_t));
            mono.pcmData.resize(frames * sizeof(int16_t));
            int16_t* dst = reinterpret_cast<int16_t*>(mono.pcmData.data());
            for (size_t i = 0; i < frames; i++) {
                int32_t l = src[i * 2 + 0];
                int32_t r = src[i * 2 + 1];
                dst[i] = static_cast<int16_t>((l + r) / 2);
            }
        } else if (stereo.bitsPerSample == 8) {
            const uint8_t* src = stereo.pcmData.data();
            size_t frames = stereo.pcmData.size() / 2;
            mono.pcmData.resize(frames);
            for (size_t i = 0; i < frames; i++) {
                uint32_t l = src[i * 2 + 0];
                uint32_t r = src[i * 2 + 1];
                mono.pcmData[i] = static_cast<uint8_t>((l + r) / 2);
            }
        } else {
            OSIRIS_ERROR("OpenALAudio: can't downmix {}-bit PCM to mono, uploading stereo as-is (won't spatialize)", stereo.bitsPerSample);
            return stereo;
        }
        return mono;
    }
}

namespace Osiris {
    bool OpenALAudio::Init() {
        m_Device = alcOpenDevice(nullptr); // nullptr = default playback device
        if (!m_Device) {
            OSIRIS_ERROR("OpenALAudio: alcOpenDevice failed (no audio device?)");
            return false;
        }

        m_Context = alcCreateContext(m_Device, nullptr);
        if (!m_Context || !alcMakeContextCurrent(m_Context)) {
            OSIRIS_ERROR("OpenALAudio: alcCreateContext/alcMakeContextCurrent failed");
            return false;
        }

        // OpenAL's default distance model (Inverse Distance Clamped) treats AL_MAX_DISTANCE as
        // "stop increasing attenuation here", not "silent here" — gain plateaus at whatever
        // value corresponds to maxDistance and stays audible forever past it, which reads as
        // "max distance doesn't work" in practice. Linear Distance Clamped instead falls off
        // to exactly 0 gain at maxDistance and stays silent beyond it — the behavior actually
        // wanted for AudioSourceComponent::maxDistance.
        alDistanceModel(AL_LINEAR_DISTANCE_CLAMPED);

        alGenSources(1, &m_TestSource);
        if (!CheckALError("alGenSources")) return false;

        OSIRIS_INFO("OpenALAudio: initialized (device: {})", alcGetString(m_Device, ALC_DEVICE_SPECIFIER));
        return true;
    }

    void OpenALAudio::Shutdown() {
        if (m_TestSource != 0) {
            alDeleteSources(1, &m_TestSource);
            m_TestSource = 0;
        }
        // Sources reference buffers, so delete them first.
        for (ALuint source : m_Sources) {
            if (source != 0) alDeleteSources(1, &source);
        }
        m_Sources.clear();

        for (ALuint buffer : m_Buffers) {
            if (buffer != 0) alDeleteBuffers(1, &buffer);
        }
        m_Buffers.clear();

        alcMakeContextCurrent(nullptr);
        if (m_Context) {
            alcDestroyContext(m_Context);
            m_Context = nullptr;
        }
        if (m_Device) {
            alcCloseDevice(m_Device);
            m_Device = nullptr;
        }
    }

    AudioBufferHandle OpenALAudio::CreateBuffer(const PCMAudioData& data) {
        if (data.pcmData.empty()) {
            OSIRIS_ERROR("OpenALAudio: CreateBuffer called with empty PCM data");
            return AudioBufferHandle{};
        }

        PCMAudioData downmixed;
        const PCMAudioData* upload = &data;
        if (data.channels == 2) {
            downmixed = DownmixToMono(data);
            upload = &downmixed;
            OSIRIS_INFO("OpenALAudio: downmixed stereo buffer to mono for 3D spatialization ({} Hz, {} -> {} bytes)",
                data.sampleRate, data.pcmData.size(), downmixed.pcmData.size());
        } else if (data.channels != 1) {
            OSIRIS_ERROR("OpenALAudio: CreateBuffer unsupported channel count: {}", data.channels);
            return AudioBufferHandle{};
        }

        ALuint buffer = 0;
        alGenBuffers(1, &buffer);
        if (!CheckALError("alGenBuffers")) return AudioBufferHandle{};

        ALenum format = PCMFormat(upload->channels, upload->bitsPerSample);
        alBufferData(buffer, format, upload->pcmData.data(),
            static_cast<ALsizei>(upload->pcmData.size()), static_cast<ALsizei>(upload->sampleRate));
        if (!CheckALError("alBufferData")) {
            alDeleteBuffers(1, &buffer);
            return AudioBufferHandle{};
        }

        uint32_t index = AllocateSlot(m_Buffers, buffer, [](ALuint b) { return b == 0; });
        return AudioBufferHandle{ index };
    }

    void OpenALAudio::DestroyBuffer(AudioBufferHandle handle) {
        if (!handle.IsValid() || handle.id >= m_Buffers.size() || m_Buffers[handle.id] == 0) return;
        alDeleteBuffers(1, &m_Buffers[handle.id]);
        m_Buffers[handle.id] = 0;
    }

    void OpenALAudio::PlaySound(AudioBufferHandle handle) {
        if (!handle.IsValid() || handle.id >= m_Buffers.size() || m_Buffers[handle.id] == 0) {
            OSIRIS_ERROR("OpenALAudio: PlaySound called with an invalid buffer handle");
            return;
        }

        alSourceStop(m_TestSource);
        alSourcei(m_TestSource, AL_BUFFER, static_cast<ALint>(m_Buffers[handle.id]));
        alSourcePlay(m_TestSource);
        CheckALError("alSourcePlay");
    }

    AudioSourceHandle OpenALAudio::CreateSource(const AudioSourceDesc& desc) {
        if (!desc.buffer.IsValid() || desc.buffer.id >= m_Buffers.size() || m_Buffers[desc.buffer.id] == 0) {
            OSIRIS_ERROR("OpenALAudio: CreateSource called with an invalid buffer handle");
            return AudioSourceHandle{};
        }

        ALuint source = 0;
        alGenSources(1, &source);
        if (!CheckALError("alGenSources (CreateSource)")) return AudioSourceHandle{};

        alSourcei(source, AL_BUFFER, static_cast<ALint>(m_Buffers[desc.buffer.id]));
        alSourcef(source, AL_GAIN, desc.gain);
        alSourcef(source, AL_PITCH, desc.pitch);
        alSourcei(source, AL_LOOPING, desc.loop ? AL_TRUE : AL_FALSE);
        alSourcef(source, AL_REFERENCE_DISTANCE, desc.referenceDistance);
        alSourcef(source, AL_MAX_DISTANCE, desc.maxDistance);
        alSourcef(source, AL_ROLLOFF_FACTOR, desc.rolloffFactor);
        CheckALError("alSource* (CreateSource)");

        uint32_t index = AllocateSlot(m_Sources, source, [](ALuint s) { return s == 0; });
        return AudioSourceHandle{ index };
    }

    void OpenALAudio::DestroySource(AudioSourceHandle handle) {
        if (!handle.IsValid() || handle.id >= m_Sources.size() || m_Sources[handle.id] == 0) return;
        alDeleteSources(1, &m_Sources[handle.id]);
        m_Sources[handle.id] = 0;
    }

    void OpenALAudio::SetSourcePosition(AudioSourceHandle handle, const glm::vec3& position) {
        if (!handle.IsValid() || handle.id >= m_Sources.size() || m_Sources[handle.id] == 0) return;
        alSource3f(m_Sources[handle.id], AL_POSITION, position.x, position.y, position.z);
    }

    void OpenALAudio::PlaySource(AudioSourceHandle handle) {
        if (!handle.IsValid() || handle.id >= m_Sources.size() || m_Sources[handle.id] == 0) return;
        alSourcePlay(m_Sources[handle.id]);
    }

    void OpenALAudio::StopSource(AudioSourceHandle handle) {
        if (!handle.IsValid() || handle.id >= m_Sources.size() || m_Sources[handle.id] == 0) return;
        alSourceStop(m_Sources[handle.id]);
    }

    bool OpenALAudio::IsSourcePlaying(AudioSourceHandle handle) const {
        if (!handle.IsValid() || handle.id >= m_Sources.size() || m_Sources[handle.id] == 0) return false;
        ALint state = AL_STOPPED;
        alGetSourcei(m_Sources[handle.id], AL_SOURCE_STATE, &state);
        return state == AL_PLAYING;
    }

    void OpenALAudio::SetListenerTransform(const glm::vec3& position, const glm::vec3& forward, const glm::vec3& up) {
        alListener3f(AL_POSITION, position.x, position.y, position.z);
        ALfloat orientation[6] = { forward.x, forward.y, forward.z, up.x, up.y, up.z };
        alListenerfv(AL_ORIENTATION, orientation);
    }
}
