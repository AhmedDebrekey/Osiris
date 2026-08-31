//
// Created by ahtal on 24/07/2026.
//

#ifndef OSIRIS_TEXTURELOADER_H
#define OSIRIS_TEXTURELOADER_H

#include <string>
#include <vector>
#include "rhi/RHI.h"

namespace Osiris {
    // CPU-side float pixel data (RGBA32F) for an HDR image — no GPU upload,
    // since IBL is the only current consumer and it needs the raw floats to
    // build its own Vulkan resources directly (see VulkanRHI::LoadEnvironmentMap).
    struct HDRImageData {
        int width  = 0;
        int height = 0;
        std::vector<float> pixels;
    };

    class TextureLoader {
    public:
        static TextureHandle LoadFromFile(const std::string& path, IRHI* rhi);
        static void ClearCache(IRHI* rhi);
        static HDRImageData LoadHDR(const std::string& path);
    };
} // Osiris

#endif //OSIRIS_TEXTURELOADER_H
