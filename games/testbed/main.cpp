#define SDL_MAIN_HANDLED

#include "../../engine/core/Engine.h"
#include "assets/SceneLoader.h"
#include "assets/TextureLoader.h"
#include "core/AssetManager.h"
#include "scene/Scene.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

namespace {
    struct LaunchConfig {
        std::string scene = "scenes/ctf.json";
        std::string environment = "hdr/EveningRoad.hdr";
        bool autoPlay = false;
        uint32_t maxFps = 120;
        bool showFps = true;
    };

    LaunchConfig LoadLaunchConfig() {
        LaunchConfig config;
        std::ifstream file(Osiris::AssetManager::GetPath("game.json"));
        if (!file.is_open()) return config;

        const nlohmann::json json = nlohmann::json::parse(file, nullptr, false);
        if (json.is_discarded()) {
            APP_ERROR("Failed to parse assets/game.json");
            return config;
        }

        config.scene = json.value("scene", config.scene);
        config.environment = json.value("environment", config.environment);
        config.autoPlay = json.value("autoPlay", config.autoPlay);
        const int configuredMaxFps = json.value("maxFps", static_cast<int>(config.maxFps));
        config.maxFps = static_cast<uint32_t>(std::max(configuredMaxFps, 0));
        config.showFps = json.value("showFps", config.showFps);
        return config;
    }
}

int main() {
    std::cout << "Osiris Engine" << std::endl;
    Osiris::Engine engine;

    if (!engine.Initialize({}, OSIRIS_GAME_ASSET_ROOT)) {
        OSIRIS_ERROR("Failed to initialize engine!");
        return -1;
    }

    const LaunchConfig launchConfig = LoadLaunchConfig();
    engine.SetMaxFps(launchConfig.maxFps);
    engine.SetFpsVisible(launchConfig.showFps);
    Osiris::HDRImageData environmentHDR = Osiris::TextureLoader::LoadHDR(
        Osiris::AssetManager::GetPath(launchConfig.environment));
    if (!environmentHDR.pixels.empty()) {
        engine.GetRHI()->LoadEnvironmentMap(
            environmentHDR.pixels.data(),
            static_cast<uint32_t>(environmentHDR.width),
            static_cast<uint32_t>(environmentHDR.height));
    }

    Osiris::Scene scene;

    Osiris::SceneLoader::Load(
        Osiris::AssetManager::GetPath(launchConfig.scene),
        scene,
        engine.GetRHI(),
        engine.GetAudio());
    if (launchConfig.autoPlay) {
        engine.SetEditorEnabled(false);
        engine.EnterPlayMode(scene);
    } else {
        scene.CreatePhysicsBodies(engine.GetPhysics());
        scene.CreateCharacters(engine.GetPhysics());
        scene.CreateAudioSources(engine.GetAudio());
        scene.StopAllAudioSources(engine.GetAudio());
        scene.CreateScriptInstances(engine.GetScripting());
    }

    while (engine.IsRunning()) {
        engine.RunFrame(scene);
    }

    engine.Shutdown();
    return 0;
}
