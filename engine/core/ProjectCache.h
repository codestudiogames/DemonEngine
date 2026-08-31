#pragma once
// ==============================================================================
//  DemonEngine::ProjectCache
//  Creates Unity/Unreal-style project-local cache folders for play mode.
// ==============================================================================
#include "DemonPCH.h"
#include "ProjectSettings.h"

namespace Demon {

class Scene;

class ProjectCache {
public:
    struct PackageStats {
        uint32_t fileCount = 0;
        uint64_t byteCount = 0;
    };

    struct BuildPackageResult {
        std::filesystem::path buildRoot;
        std::filesystem::path dataRoot;
        std::filesystem::path executablePath;
        std::filesystem::path gameModulePath;
        std::filesystem::path assetPackagePath;
        std::filesystem::path shaderPackagePath;
        std::filesystem::path manifestPath;
        std::filesystem::path runtimeConfigPath;
        std::filesystem::path startupScenePath;
        std::filesystem::path looseAssetsRoot;
        std::vector<std::filesystem::path> runtimeDlls;
        PackageStats assets;
        PackageStats shaders;
    };

    static std::filesystem::path getLibraryRoot(const std::filesystem::path& projectRoot);
    static std::filesystem::path getShaderCacheRoot(const std::filesystem::path& projectRoot);
    static std::filesystem::path getAssetCacheRoot(const std::filesystem::path& projectRoot);
    static std::filesystem::path getThumbnailCacheRoot(const std::filesystem::path& projectRoot);
    static std::filesystem::path getAssetDatabaseRoot(const std::filesystem::path& projectRoot);

    static bool ensureLayout(const std::filesystem::path& projectRoot);
    static bool ensureProjectDatabases(const std::filesystem::path& projectRoot);
    static bool compileShaders(const std::filesystem::path& projectRoot,
                               const std::filesystem::path& shaderSourceDir,
                               const std::filesystem::path& shaderCompilerExe);
    static bool packAssets(const std::filesystem::path& assetsRoot,
                           const std::filesystem::path& outputPackage,
                           PackageStats* stats = nullptr);
    static bool packCompiledShaders(const std::filesystem::path& shaderCacheRoot,
                                    const std::filesystem::path& outputPackage,
                                    PackageStats* stats = nullptr);
    static bool writeGlobalIlluminationBakeManifest(const std::filesystem::path& projectRoot,
                                                    std::string_view mode,
                                                    std::string_view projectName);
    static bool buildWindowsPackage(const std::filesystem::path& projectRoot,
                                    const std::filesystem::path& assetsRoot,
                                    const std::filesystem::path& shaderSourceDir,
                                    const std::filesystem::path& shaderCompilerExe,
                                    const std::filesystem::path& currentExecutable,
                                    const ProjectSettings::BuildConfig& config,
                                    BuildPackageResult* result = nullptr);
    static bool writeSceneAssetManifest(const std::filesystem::path& projectRoot, const Scene& scene);
    static bool writeCacheManifest(const std::filesystem::path& projectRoot, std::string_view projectName);
};

} // namespace Demon
