#define SDL_MAIN_HANDLED

#include "../../engine/core/Engine.h"
#include "assets/SceneLoader.h"
#include "assets/TextureLoader.h"
#include "core/AssetManager.h"
#include "scene/Scene.h"

#include <iostream>

int main() {
    std::cout << "Osiris Engine" << std::endl;
    Osiris::Engine engine;

    if (!engine.Initialize()) {
        OSIRIS_ERROR("Failed to initialize engine!");
        return -1;
    }

    Osiris::HDRImageData environmentHDR = Osiris::TextureLoader::LoadHDR(
        Osiris::AssetManager::GetPath("hdr/EveningRoad.hdr"));
    if (!environmentHDR.pixels.empty()) {
        engine.GetRHI()->LoadEnvironmentMap(
            environmentHDR.pixels.data(),
            static_cast<uint32_t>(environmentHDR.width),
            static_cast<uint32_t>(environmentHDR.height));
    }

    Osiris::Scene scene;

    Osiris::SceneLoader::Load(
        Osiris::AssetManager::GetPath("scenes/ctf.json"),
        scene,
        engine.GetRHI(),
        engine.GetAudio());
    scene.CreatePhysicsBodies(engine.GetPhysics());
    scene.CreateCharacters(engine.GetPhysics());
    scene.CreateAudioSources(engine.GetAudio());
    scene.StopAllAudioSources(engine.GetAudio());
    scene.CreateScriptInstances(engine.GetScripting());

    while (engine.IsRunning()) {
        engine.RunFrame(scene);
    }

    engine.Shutdown();
    return 0;
}
