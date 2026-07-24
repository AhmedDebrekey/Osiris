//
// Created by ahtal on 24/07/2026.
//

#include "TextureLoader.h"
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "core/Log.h"

namespace Osiris {
    TextureHandle TextureLoader::LoadFromFile(const std::string &path, IRHI *rhi) {
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
        return handle;
    }

} // Osiris