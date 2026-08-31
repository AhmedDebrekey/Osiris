//
// Created by Debreky on 25/07/2026.
//

#ifndef OSIRIS_SCENELOADER_H
#define OSIRIS_SCENELOADER_H

#include <string>
#include "scene/Scene.h"
#include "rhi/RHI.h"
#include "audio/IAudio.h"

namespace Osiris {

    class SceneLoader {
    public:
        // Populates scene from a JSON file. Mesh entities go through Scene::SpawnModel (the
        // same path Lua/the Asset Browser use), while box entities use cached procedural meshes
        // and materials. Doesn't create live physics bodies/characters/
        // audio sources/script instances. Call Scene::CreatePhysicsBodies/CreateCharacters/
        // CreateAudioSources/CreateScriptInstances afterward, same as scene setup already does.
        static void Load(const std::string& path, Scene& scene, IRHI* rhi, IAudio* audio);

        // Writes scene back out to a JSON file matching Load's format. Entities with a
        // ModelSourceComponent (i.e. spawned via Scene::SpawnModel) keep one "mesh" entry per
        // spawn call plus lightweight child override entries, allowing components and transforms
        // edited on generated glTF nodes to persist. BoxSourceComponent preserves procedural box
        // inputs. A raw MeshComponent without either source component still has no recoverable
        // provenance.
        static bool Save(const std::string& path, Scene& scene);
    };

} // Osiris

#endif //OSIRIS_SCENELOADER_H
