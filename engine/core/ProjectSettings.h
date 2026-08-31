#pragma once
// ==============================================================================
//  DemonEngine::ProjectSettings
//  Per-project editor/runtime metadata persisted in the project config JSON.
// ==============================================================================
#include "DemonPCH.h"

namespace Demon {

class ProjectSettings {
public:
    struct BuildConfig {
        std::string appName;
        std::string companyName = "Code Studio Games";
        std::string appVersion = "1.0.0";
        std::string outputName;
        std::string startupScene;
        std::string bundleId;
        std::string buildDirectory = "Build/Windows";
        bool compileShadersOnBuild = true;
        bool packageAssets = true;
        bool packageShaders = true;
        bool copyExecutable = true;
    };

    struct ScriptingConfig {
        std::string language = "DemonScript";
        std::string extension = ".demonscript";
        std::filesystem::path idePath;
        bool autoCompileOnFocus = true;
        bool hotReloadEnabled = true;
    };

    bool load(const std::filesystem::path& path, std::string_view fallbackProjectName = {});
    bool save(const std::filesystem::path& path) const;
    void applyProjectDefaults(std::string_view projectName);

    BuildConfig build;
    ScriptingConfig scripting;
};

} // namespace Demon
