//
// Created by ahtal on 24/07/2026.
//

#include "TextureLoader.h"
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <filesystem>
#include <unordered_map>

#include "core/AssetManager.h"
#include "core/Log.h"

namespace Osiris {
    namespace {
        std::unordered_map<IRHI*, std::unordered_map<std::string, TextureHandle>> s_TextureCaches;
    }

    TextureHandle TextureLoader::LoadFromFile(const std::string &path, IRHI *rhi) {
        const std::string cacheKey = AssetManager::NormalizePathKey(path);
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
        uint64_t imageSize = width * height * 4;


        TextureDesc textureDesc = {
            .pixels = pixels,
            .dataSize = imageSize,
            .width = static_cast<uint32_t>(width),
            .height = static_cast<uint32_t>(height),
            .format = TextureFormat::RGBA8_UNORM,
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
