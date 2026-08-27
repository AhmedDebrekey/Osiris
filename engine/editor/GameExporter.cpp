#include "GameExporter.h"

#include "assets/SceneLoader.h"
#include "core/AssetManager.h"
#include "scene/Scene.h"

#include <SDL.h>
#include <nlohmann/json.hpp>

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace Osiris {
    namespace {
        namespace fs = std::filesystem;

        bool IsValidGameName(const std::string& gameName) {
            if (gameName.empty()) return false;
            for (const unsigned char character : gameName) {
                if (!std::isalnum(character) && character != ' ' && character != '-' && character != '_') {
                    return false;
                }
            }
            return gameName != "." && gameName != "..";
        }

        std::string EscapePowerShellLiteral(std::string value) {
            size_t position = 0;
            while ((position = value.find('\'', position)) != std::string::npos) {
                value.insert(position, 1, '\'');
                position += 2;
            }
            return value;
        }

        fs::path FindRunningExecutable(const fs::path& binaryDirectory) {
            const fs::path testbedExecutable = binaryDirectory / "Testbed.exe";
            if (fs::is_regular_file(testbedExecutable)) return testbedExecutable;

            for (const fs::directory_entry& entry : fs::directory_iterator(binaryDirectory)) {
                if (entry.is_regular_file() && entry.path().extension() == ".exe") {
                    return entry.path();
                }
            }
            return {};
        }

        bool WriteLaunchConfig(const fs::path& assetDirectory, const std::string& gameName,
                               uint32_t maxFps, bool showFps) {
            const nlohmann::json config = {
                {"name", gameName},
                {"scene", "scenes/exported_game.json"},
                {"environment", "hdr/EveningRoad.hdr"},
                {"autoPlay", true},
                {"maxFps", maxFps},
                {"showFps", showFps},
            };

            std::ofstream file(assetDirectory / "game.json");
            if (!file.is_open()) return false;
            file << config.dump(2);
            return file.good();
        }

        bool WriteReadme(const fs::path& packageDirectory, const std::string& gameName) {
            std::ofstream file(packageDirectory / "README.txt");
            if (!file.is_open()) return false;
            file << gameName << "\n\n"
                 << "1. Extract the complete ZIP before running the game.\n"
                 << "2. Keep the assets folder and DLL files beside the executable.\n"
                 << "3. Run " << gameName << ".exe.\n\n"
                 << "Requirements:\n"
                 << "- Windows 64-bit\n"
                 << "- A Vulkan 1.4-capable graphics driver\n"
                 << "- Microsoft Visual C++ 2015-2022 Redistributable (x64)\n";
            return file.good();
        }
    }

    GameExportResult GameExporter::Export(Scene& scene, const std::string& gameName,
                                          uint32_t maxFps, bool showFps) {
#ifndef NDEBUG
        return {false, "Run the Release configuration before exporting a distributable game."};
#endif

        try {
            if (!IsValidGameName(gameName)) {
                return {false, "Use only letters, numbers, spaces, hyphens, and underscores in the game name."};
            }

            const fs::path sourceAssets = fs::absolute(AssetManager::GetAssetRoot()).lexically_normal();
            if (!fs::is_directory(sourceAssets)) {
                return {false, "The active assets directory could not be found."};
            }

            char* basePath = SDL_GetBasePath();
            if (!basePath) return {false, "The executable directory could not be determined."};
            const fs::path binaryDirectory = fs::path(basePath).lexically_normal();
            SDL_free(basePath);

            const fs::path sourceExecutable = FindRunningExecutable(binaryDirectory);
            if (sourceExecutable.empty()) {
                return {false, "The running executable could not be found."};
            }

            const fs::path exportRoot = sourceAssets.parent_path() / "exports";
            const fs::path packageDirectory = exportRoot / gameName;
            const fs::path packageAssets = packageDirectory / "assets";
            const fs::path zipPath = exportRoot / (gameName + "-win64.zip");

            std::error_code error;
            fs::create_directories(exportRoot, error);
            if (error) return {false, "Could not create the exports directory: " + error.message()};

            fs::remove_all(packageDirectory, error);
            if (error) return {false, "Could not replace the previous export folder: " + error.message()};

            fs::create_directories(packageDirectory, error);
            if (error) return {false, "Could not create the game package folder: " + error.message()};

            fs::copy(sourceAssets, packageAssets,
                fs::copy_options::recursive | fs::copy_options::overwrite_existing, error);
            if (error) return {false, "Could not copy the assets: " + error.message()};

            fs::copy_file(sourceExecutable, packageDirectory / (gameName + ".exe"),
                fs::copy_options::overwrite_existing, error);
            if (error) return {false, "Could not copy the executable: " + error.message()};

            for (const fs::directory_entry& entry : fs::directory_iterator(binaryDirectory)) {
                if (!entry.is_regular_file() || entry.path().extension() != ".dll") continue;
                fs::copy_file(entry.path(), packageDirectory / entry.path().filename(),
                    fs::copy_options::overwrite_existing, error);
                if (error) return {false, "Could not copy runtime DLLs: " + error.message()};
            }
            if (!fs::is_regular_file(packageDirectory / "SDL2.dll")
                || !fs::is_regular_file(packageDirectory / "OpenAL32.dll")) {
                return {false, "SDL2.dll or OpenAL32.dll is missing from the Release output."};
            }

            const fs::path exportedScene = packageAssets / "scenes" / "exported_game.json";
            if (!SceneLoader::Save(exportedScene.generic_string(), scene)) {
                return {false, "The current scene could not be saved into the export."};
            }
            if (!WriteLaunchConfig(packageAssets, gameName, maxFps, showFps)) {
                return {false, "The exported launch configuration could not be written."};
            }
            if (!WriteReadme(packageDirectory, gameName)) {
                return {false, "The exported README could not be written."};
            }

            const std::string packageLiteral = EscapePowerShellLiteral(packageDirectory.string());
            const std::string zipLiteral = EscapePowerShellLiteral(zipPath.string());
            const std::string zipCommand =
                "powershell.exe -NoProfile -NonInteractive -Command \"Compress-Archive -LiteralPath '"
                + packageLiteral + "' -DestinationPath '" + zipLiteral + "' -Force\"";
            if (std::system(zipCommand.c_str()) != 0 || !fs::is_regular_file(zipPath)) {
                return {false, "The game folder was exported, but Windows could not create the ZIP."};
            }

            return {true, "Exported " + zipPath.generic_string()};
        } catch (const std::exception& error) {
            return {false, "Export failed: " + std::string(error.what())};
        }
    }
}
