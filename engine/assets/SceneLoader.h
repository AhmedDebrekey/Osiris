//
// Created by Debreky on 25/07/2026.
//

#ifndef OSIRIS_SCENELOADER_H
#define OSIRIS_SCENELOADER_H

#include <string>
#include "scene/Scene.h"
#include "rhi/RHI.h"

namespace Osiris {

    class SceneLoader {
    public:
        static void Load(const std::string& path, Scene& scene, IRHI* rhi);
    };

} // Osiris

#endif //OSIRIS_SCENELOADER_H