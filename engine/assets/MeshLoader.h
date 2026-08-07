//
// Created by Debreky on 21/07/2026.
//

#ifndef OSIRIS_MESHLOADER_H
#define OSIRIS_MESHLOADER_H
#include "renderer/MeshType.h"
#include "rhi/RHI.h"
#include <string>

namespace Osiris {
    class MeshLoader {
        public:
            static Mesh LoadFromGLTF(const std::string& path, IRHI* rhi);
            static Mesh CreatePlane(float width, float height, IRHI* rhi);

    };
} // Osiris

#endif //OSIRIS_MESHLOADER_H