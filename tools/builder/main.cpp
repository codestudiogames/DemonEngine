#include "core/Logger.h"
#include "core/ProjectCache.h"
#include "core/ProjectSettings.h"

#include <filesystem>
#include <string>

namespace fs = std::filesystem;

using namespace Demon;

namespace {

std::string argValue(int& index, int argc, char** argv)
{
    if (index + 1 >= argc)
        return {};
    return argv[++index];
}

void printUsage()
{
    DEMON_LOG_INFO("Usage: DemonBuilder --project-root <dir> --assets <dir> --shaders <dir> --compiler <exe> --exe <DemonGamePlayer.exe> [--config <project.demonproj|legacy.json>] [--app <name>] [--output <name>] [--build-dir <dir>]");
}

} // namespace

int main(int argc, char** argv)
{
    Logger::init();

    fs::path projectRoot;
    fs::path assetsRoot;
    fs::path shaderSourceDir;
    fs::path shaderCompiler;
    fs::path executable;
    fs::path configPath;
    ProjectSettings settings;
    settings.applyProjectDefaults("DemonGame");

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--project-root") projectRoot = argValue(i, argc, argv);
        else if (arg == "--assets") assetsRoot = argValue(i, argc, argv);
        else if (arg == "--shaders") shaderSourceDir = argValue(i, argc, argv);
        else if (arg == "--compiler") shaderCompiler = argValue(i, argc, argv);
        else if (arg == "--exe") executable = argValue(i, argc, argv);
        else if (arg == "--config") configPath = argValue(i, argc, argv);
        else if (arg == "--app") settings.build.appName = argValue(i, argc, argv);
        else if (arg == "--output") settings.build.outputName = argValue(i, argc, argv);
        else if (arg == "--company") settings.build.companyName = argValue(i, argc, argv);
        else if (arg == "--version") settings.build.appVersion = argValue(i, argc, argv);
        else if (arg == "--startup") settings.build.startupScene = argValue(i, argc, argv);
        else if (arg == "--build-dir") settings.build.buildDirectory = argValue(i, argc, argv);
        else if (arg == "--no-compile-shaders") settings.build.compileShadersOnBuild = false;
        else if (arg == "--help") {
            printUsage();
            return 0;
        }
    }

    if (!configPath.empty() && fs::exists(configPath))
        settings.load(configPath, settings.build.appName);
    settings.applyProjectDefaults(settings.build.appName);

    if (projectRoot.empty() || assetsRoot.empty() || shaderSourceDir.empty() || shaderCompiler.empty() || executable.empty()) {
        printUsage();
        return 1;
    }

    ProjectCache::BuildPackageResult result{};
    if (!ProjectCache::buildWindowsPackage(projectRoot,
                                           assetsRoot,
                                           shaderSourceDir,
                                           shaderCompiler,
                                           executable,
                                           settings.build,
                                           &result))
    {
        DEMON_LOG_ERROR("DemonBuilder: Windows package build failed.");
        return 1;
    }

    DEMON_LOG_INFO("DemonBuilder: build root '{}'", result.buildRoot.string());
    DEMON_LOG_INFO("DemonBuilder: executable '{}'", result.executablePath.string());
    DEMON_LOG_INFO("DemonBuilder: game module '{}'", result.gameModulePath.string());
    DEMON_LOG_INFO("DemonBuilder: assets {} file(s) -> '{}'", result.assets.fileCount, result.assetPackagePath.string());
    DEMON_LOG_INFO("DemonBuilder: shaders {} file(s) -> '{}'", result.shaders.fileCount, result.shaderPackagePath.string());
    return 0;
}
