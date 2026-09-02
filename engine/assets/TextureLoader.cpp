//
// Created by ahtal on 24/07/2026.
//

#include "TextureLoader.h"
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <algorithm>
#include <bit>
#include <filesystem>
#include <unordered_map>

#include "core/AssetManager.h"
#include "core/Log.h"

namespace Osiris {
    namespace {
        struct TextureCacheKey {
            std::string path;
            TextureFormat format;

            bool operator==(const TextureCacheKey&) const = default;
        };

        struct TextureCacheKeyHash {
            size_t operator()(const TextureCacheKey& key) const noexcept {
                size_t hash = std::hash<std::string>{}(key.path);
                hash ^= std::hash<uint32_t>{}(static_cast<uint32_t>(key.format))
                      + 0x9e3779b9 + (hash << 6) + (hash >> 2);
                return hash;
            }
        };

        using TextureCache = std::unordered_map<TextureCacheKey, TextureHandle, TextureCacheKeyHash>;
        std::unordered_map<IRHI*, TextureCache> s_TextureCaches;
    }

    TextureHandle TextureLoader::LoadFromFile(const std::string& path, IRHI* rhi,
                                               TextureFormat format) {
        const TextureCacheKey cacheKey = {
            .path = AssetManager::NormalizePathKey(path),
            .format = format,
        };
        auto& cache = s_TextureCaches[rhi];
        if (const auto cached = cache.find(cacheKey); cached != cache.end()) {
            return cached->second;
        }

        int width, height, channels;
        stbi_uc* pixels = stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
        if (!pixels) {
            OSIRIS_ERROR("Failed to load texture: {} — {}", path, stbi_failure_reason());
            return TextureHandle{};
        }
        const uint64_t imageSize = static_cast<uint64_t>(width) * height * 4;
        const uint32_t mipLevels = std::bit_width(
            static_cast<uint32_t>(std::max(width, height)));

        TextureDesc textureDesc = {
            .pixels = pixels,
            .dataSize = imageSize,
            .width = static_cast<uint32_t>(width),
            .height = static_cast<uint32_t>(height),
            .mipLevels = mipLevels,
            .format = format,
        };
        TextureHandle handle = rhi->CreateTexture(textureDesc);
        stbi_image_free(pixels);
        if (handle.IsValid()) {
            cache.emplace(cacheKey, handle);
        }
        return handle;
    }

    void TextureLoader::ClearCache(IRHI* rhi) {
        s_TextureCaches.erase(rhi);
    }

    HDRImageData TextureLoader::LoadHDR(const std::string& path) {
        HDRImageData result;

        int width, height, channels;
        float* pixels = stbi_loadf(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
        if (!pixels) {
            OSIRIS_ERROR("Failed to load HDR image: {} — {}", path, stbi_failure_reason());
            return result;
        }

        result.width  = width;
        result.height = height;
        result.pixels.assign(pixels, pixels + (static_cast<size_t>(width) * height * 4));

        stbi_image_free(pixels);
        return result;
    }

} // Osiris
