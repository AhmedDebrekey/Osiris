//
// Created by ahtal on 24/07/2026.
//

#ifndef OSIRIS_TEXTURELOADER_H
#define OSIRIS_TEXTURELOADER_H

#include <string>
#include "rhi/RHI.h"

namespace Osiris {
    class TextureLoader {
    public:
        static TextureHandle LoadFromFile(const std::string& path, IRHI* rhi);
    };
} // Osiris

#endif //OSIRIS_TEXTURELOADER_H