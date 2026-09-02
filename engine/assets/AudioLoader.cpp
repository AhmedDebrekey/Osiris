#include "AudioLoader.h"

#include <algorithm>
#include <cstring>
#include <fstream>

#include "core/Log.h"

namespace {
    uint32_t ReadU32LE(std::ifstream& file) {
        uint8_t b[4] = {};
        file.read(reinterpret_cast<char*>(b), 4);
        return static_cast<uint32_t>(b[0]) | (static_cast<uint32_t>(b[1]) << 8)
             | (static_cast<uint32_t>(b[2]) << 16) | (static_cast<uint32_t>(b[3]) << 24);
    }

    uint16_t ReadU16LE(std::ifstream& file) {
        uint8_t b[2] = {};
        file.read(reinterpret_cast<char*>(b), 2);
        return static_cast<uint16_t>(b[0]) | static_cast<uint16_t>(b[1] << 8);
    }

    bool ReadFourCC(std::ifstream& file, char out[4]) {
        file.read(out, 4);
        return file.good();
    }
}

namespace Osiris {
    PCMAudioData AudioLoader::LoadWAV(const std::string& path) {
        PCMAudioData result;

        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            OSIRIS_ERROR("AudioLoader: failed to open WAV file: {}", path);
            return {};
        }

        file.seekg(0, std::ios::end);
        const std::streamoff fileSize = file.tellg();
        file.seekg(0, std::ios::beg);

        char riffId[4], waveId[4];
        ReadFourCC(file, riffId);
        ReadU32LE(file); // RIFF chunk size is unused, individual chunk sizes drive parsing.
        ReadFourCC(file, waveId);
        if (!file.good() || std::memcmp(riffId, "RIFF", 4) != 0 || std::memcmp(waveId, "WAVE", 4) != 0) {
            OSIRIS_ERROR("AudioLoader: not a RIFF/WAVE file: {}", path);
            return {};
        }

        bool haveFmt = false;
        while (file.good()) {
            char chunkId[4];
            if (!ReadFourCC(file, chunkId)) break; // clean EOF between chunks
            uint32_t chunkSize = ReadU32LE(file);
            if (!file.good()) break;

            if (std::memcmp(chunkId, "fmt ", 4) == 0) {
                uint16_t audioFormat = ReadU16LE(file);
                result.channels      = ReadU16LE(file);
                result.sampleRate    = ReadU32LE(file);
                ReadU32LE(file); // byte rate — derivable, unused
                ReadU16LE(file); // block align — derivable, unused
                result.bitsPerSample = ReadU16LE(file);

                if (audioFormat != 1) { // 1 == WAVE_FORMAT_PCM
                    OSIRIS_ERROR("AudioLoader: only uncompressed PCM WAV is supported (format {}): {}", audioFormat, path);
                    return {};
                }
                haveFmt = true;

                // The fmt chunk can carry extra bytes past the 16 we just read.
                if (chunkSize > 16) file.seekg(chunkSize - 16, std::ios::cur);
            } else if (std::memcmp(chunkId, "data", 4) == 0) {
                const std::streamoff dataStart = file.tellg();
                if (dataStart < 0 || dataStart > fileSize) {
                    OSIRIS_ERROR("AudioLoader: invalid WAV data offset: {}", path);
                    return {};
                }

                const uint64_t availableBytes = static_cast<uint64_t>(fileSize - dataStart);
                const uint64_t dataBytes = std::min<uint64_t>(chunkSize, availableBytes);
                if (dataBytes != chunkSize) {
                    OSIRIS_WARN("AudioLoader: WAV data size {} exceeds the {} bytes available, clamping: {}",
                        chunkSize, availableBytes, path);
                }

                result.pcmData.resize(static_cast<size_t>(dataBytes));
                file.read(reinterpret_cast<char*>(result.pcmData.data()),
                    static_cast<std::streamsize>(dataBytes));
            } else {
                file.seekg(chunkSize, std::ios::cur); // Skip unknown chunks such as LIST or fact.
            }

            if (chunkSize % 2 != 0) file.seekg(1, std::ios::cur); // chunks are word-aligned
        }

        if (!haveFmt || result.pcmData.empty()) {
            OSIRIS_ERROR("AudioLoader: WAV file missing fmt/data chunk: {}", path);
            return {};
        }

        OSIRIS_INFO("AudioLoader: loaded {} ({} Hz, {} ch, {}-bit, {} bytes)",
            path, result.sampleRate, result.channels, result.bitsPerSample, result.pcmData.size());
        return result;
    }
}
