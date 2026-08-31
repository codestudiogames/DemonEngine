// ==============================================================================
//  DemonEngine::ProjectCache  -  Implementation
// ==============================================================================
#include "ProjectCache.h"

#include <cctype>
#include <fstream>
#include <map>
#include <set>

#include "Logger.h"
#include "scene/Scene.h"
#include "scene/SceneSerializer.h"
#include "serialization/Serialization.h"

namespace Demon {

namespace {

std::string sanitizeFileStem(std::string value, std::string_view fallback)
{
    if (value.empty())
        value = std::string(fallback);

    for (char& c : value) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (!std::isalnum(uc) && c != '_' && c != '-' && c != '.')
            c = '_';
    }

    while (!value.empty() && (value.back() == '.' || value.back() == ' '))
        value.pop_back();
    return value.empty() ? std::string(fallback) : value;
}

bool hasDemonSceneExtension(const std::filesystem::path& path)
{
    std::string extension = path.extension().string();
    for (char& ch : extension)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return extension == ".demonscene";
}

std::string escapeJson(std::string_view value)
{
    std::string escaped;
    escaped.reserve(value.size() + 8);
    for (char c : value) {
        switch (c) {
            case '\\': escaped += "\\\\"; break;
            case '"': escaped += "\\\""; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default: escaped += c; break;
        }
    }
    return escaped;
}

bool pathStartsWith(const std::filesystem::path& path, const std::filesystem::path& root)
{
    auto pathIt = path.begin();
    auto rootIt = root.begin();
    for (; rootIt != root.end(); ++rootIt, ++pathIt) {
        if (pathIt == path.end() || *pathIt != *rootIt)
            return false;
    }
    return true;
}

bool copyCompiledShaders(const std::filesystem::path& sourceDir, const std::filesystem::path& outputDir)
{
    std::error_code ec;
    if (!std::filesystem::exists(sourceDir))
        return false;

    std::filesystem::create_directories(outputDir, ec);
    for (const auto& entry : std::filesystem::directory_iterator(sourceDir, ec)) {
        if (ec || !entry.is_regular_file())
            continue;
        if (entry.path().extension() != ".cso")
            continue;
        std::filesystem::copy_file(entry.path(),
                                   outputDir / entry.path().filename(),
                                   std::filesystem::copy_options::overwrite_existing,
                                   ec);
    }

    return !ec;
}

bool reserveCacheFile(const std::filesystem::path& path, uint64_t byteSize)
{
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec)
        return false;

    if (std::filesystem::exists(path, ec) &&
        !ec &&
        std::filesystem::file_size(path, ec) >= byteSize)
    {
        return !ec;
    }

    std::fstream file(path, std::ios::in | std::ios::out | std::ios::binary);
    if (!file.is_open()) {
        std::ofstream create(path, std::ios::binary);
        create.close();
        file.open(path, std::ios::in | std::ios::out | std::ios::binary);
    }
    if (!file.is_open())
        return false;

    if (byteSize > 0) {
        file.seekp(static_cast<std::streamoff>(byteSize - 1));
        const char zero = 0;
        file.write(&zero, 1);
    }

    return file.good();
}

bool runProcessAndWait(const std::filesystem::path& executable,
                       const std::filesystem::path& workingDirectory,
                       const std::vector<std::wstring>& arguments,
                       DWORD& exitCode)
{
    if (executable.empty() || !std::filesystem::exists(executable))
        return false;

    std::wstring commandLine = std::format(L"\"{}\"", executable.wstring());
    for (const auto& argument : arguments)
        commandLine += std::format(L" \"{}\"", argument);

    std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back(L'\0');

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);

    PROCESS_INFORMATION processInfo{};
    const std::wstring workingDir = workingDirectory.empty()
        ? executable.parent_path().wstring()
        : workingDirectory.wstring();

    const BOOL created = CreateProcessW(executable.wstring().c_str(),
                                        mutableCommand.data(),
                                        nullptr,
                                        nullptr,
                                        FALSE,
                                        CREATE_NO_WINDOW,
                                        nullptr,
                                        workingDir.empty() ? nullptr : workingDir.c_str(),
                                        &startupInfo,
                                        &processInfo);
    if (!created)
        return false;

    WaitForSingleObject(processInfo.hProcess, INFINITE);
    const BOOL gotExitCode = GetExitCodeProcess(processInfo.hProcess, &exitCode);
    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    return gotExitCode == TRUE;
}

std::vector<std::filesystem::path> collectFiles(const std::filesystem::path& root,
                                                const std::unordered_set<std::string>& extensions)
{
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    if (root.empty() || !std::filesystem::exists(root, ec))
        return files;

    for (const auto& entry : std::filesystem::recursive_directory_iterator(root, ec)) {
        if (ec || !entry.is_regular_file())
            continue;
        if (!extensions.empty() && !extensions.contains(entry.path().extension().string()))
            continue;
        files.push_back(entry.path());
    }

    std::ranges::sort(files, [](const auto& a, const auto& b) {
        return a.generic_string() < b.generic_string();
    });
    return files;
}

std::filesystem::path findFirstDemonScene(const std::filesystem::path& root)
{
    std::vector<std::filesystem::path> scenes;
    std::error_code ec;
    if (root.empty() || !std::filesystem::exists(root, ec))
        return {};

    std::filesystem::recursive_directory_iterator it(
        root, std::filesystem::directory_options::skip_permission_denied, ec);
    const std::filesystem::recursive_directory_iterator end;
    for (; it != end; it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        std::error_code entryEc;
        if (!it->is_regular_file(entryEc) || entryEc)
            continue;
        if (hasDemonSceneExtension(it->path()))
            scenes.push_back(it->path());
    }

    std::ranges::sort(scenes, [](const auto& a, const auto& b) {
        return a.generic_string() < b.generic_string();
    });
    return scenes.empty() ? std::filesystem::path{} : scenes.front();
}

bool writePackageArchive(const std::filesystem::path& root,
                         const std::filesystem::path& outputPackage,
                         std::string_view magic,
                         const std::unordered_set<std::string>& extensions,
                         ProjectCache::PackageStats* stats)
{
    if (stats)
        *stats = {};
    if (root.empty() || outputPackage.empty())
        return false;

    const auto files = collectFiles(root, extensions);
    std::error_code ec;
    std::filesystem::create_directories(outputPackage.parent_path(), ec);
    if (ec)
        return false;

    BinaryWriter writer;
    if (!writer.open(outputPackage))
        return false;

    std::array<char, 8> magicBytes{};
    const size_t copyLength = std::min(magicBytes.size(), magic.size());
    std::memcpy(magicBytes.data(), magic.data(), copyLength);
    writer.writeBytes(magicBytes.data(), magicBytes.size());

    const uint32_t version = 1;
    const uint32_t entryCount = static_cast<uint32_t>(files.size());
    writer.write(version);
    writer.write(entryCount);

    ProjectCache::PackageStats localStats{};
    std::vector<char> buffer;
    for (const auto& file : files) {
        const std::filesystem::path relative = std::filesystem::relative(file, root, ec);
        const std::string relativePath = (ec ? file.filename() : relative).generic_string();
        const uint64_t size = static_cast<uint64_t>(std::filesystem::file_size(file, ec));
        if (ec)
            continue;

        const auto writeTime = std::filesystem::last_write_time(file, ec);
        const int64_t writeTimeTicks = ec ? 0 : static_cast<int64_t>(writeTime.time_since_epoch().count());

        writer.writeString(relativePath);
        writer.write(size);
        writer.write(writeTimeTicks);

        std::ifstream in(file, std::ios::binary);
        if (!in.is_open())
            return false;

        buffer.resize(static_cast<size_t>(size));
        if (size > 0) {
            in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            if (!in.good() && !in.eof())
                return false;
            writer.writeBytes(buffer.data(), buffer.size());
        }

        ++localStats.fileCount;
        localStats.byteCount += size;
    }

    if (stats)
        *stats = localStats;
    return true;
}

bool copyRuntimeDlls(const std::filesystem::path& executableDir,
                     const std::filesystem::path& buildRoot,
                     const std::string& outputName,
                     std::filesystem::path* gameModulePath,
                     std::vector<std::filesystem::path>* copiedDlls)
{
    if (gameModulePath)
        gameModulePath->clear();
    if (copiedDlls)
        copiedDlls->clear();
    if (executableDir.empty() || buildRoot.empty())
        return false;

    std::error_code ec;
    if (!std::filesystem::exists(executableDir, ec))
        return false;

    bool copiedAny = false;
    for (const auto& entry : std::filesystem::directory_iterator(executableDir, ec)) {
        if (ec || !entry.is_regular_file())
            continue;
        if (entry.path().extension() != ".dll")
            continue;

        const std::string dllName = entry.path().filename().string();
        std::filesystem::path destination = buildRoot / entry.path().filename();
        if (dllName == "DemonGameModule.dll" || dllName == "DemonGameModuled.dll")
            destination = buildRoot / (outputName + ".dll");

        std::filesystem::copy_file(entry.path(),
                                   destination,
                                   std::filesystem::copy_options::overwrite_existing,
                                   ec);
        if (ec) {
            DEMON_LOG_ERROR("ProjectCache: failed to copy runtime DLL '{}' -> '{}': {}",
                            entry.path().string(), destination.string(), ec.message());
            return false;
        }

        copiedAny = true;
        if (copiedDlls)
            copiedDlls->push_back(destination);
        if (gameModulePath && destination.filename() == outputName + ".dll")
            *gameModulePath = destination;
    }

    if (!copiedAny)
        DEMON_LOG_ERROR("ProjectCache: no runtime DLLs found in '{}'.", executableDir.string());
    return copiedAny;
}

std::filesystem::path resolveStartupScenePath(const std::filesystem::path& projectRoot,
                                              const std::filesystem::path& assetsRoot,
                                              std::string_view configuredScene)
{
    if (configuredScene.empty())
        return findFirstDemonScene(assetsRoot);

    std::filesystem::path scenePath(configuredScene);
    std::error_code ec;
    if (scenePath.is_absolute() && std::filesystem::exists(scenePath, ec))
        return scenePath;

    const std::filesystem::path projectCandidate = projectRoot / scenePath;
    ec = {};
    if (std::filesystem::exists(projectCandidate, ec))
        return projectCandidate;

    const std::filesystem::path assetsCandidate = assetsRoot / scenePath;
    ec = {};
    if (std::filesystem::exists(assetsCandidate, ec))
        return assetsCandidate;

    return {};
}

std::string sceneFileNameForExport(const std::filesystem::path& scenePath,
                                   const std::string& outputName)
{
    std::string stem = scenePath.empty() ? outputName : scenePath.stem().string();
    stem = sanitizeFileStem(stem, outputName.empty() ? "StartupScene" : outputName);
    return stem + ".demonscene";
}

bool copyLooseAssets(const std::filesystem::path& assetsRoot,
                     const std::filesystem::path& destination)
{
    if (assetsRoot.empty())
        return true;

    std::error_code ec;
    if (!std::filesystem::exists(assetsRoot, ec))
        return true;

    std::filesystem::create_directories(destination, ec);
    if (ec)
        return false;

    std::filesystem::copy(assetsRoot,
                          destination,
                          std::filesystem::copy_options::recursive |
                              std::filesystem::copy_options::overwrite_existing,
                          ec);
    return !ec;
}

// ── Portable scene export ─────────────────────────────────────────────────────
// Rewrites absolute asset references so an exported build runs on any machine:
//   - paths inside the project assets root become "assets/<relative>"
//   - paths outside the project are copied into "assets/_imported/" and remapped
// Relative and "builtin:" paths are left untouched (they already resolve against
// the packaged executable directory at runtime).
class ExportPathRemapper {
public:
    ExportPathRemapper(const std::filesystem::path& assetsRoot,
                       std::filesystem::path buildAssetsRoot)
        : m_buildAssetsRoot(std::move(buildAssetsRoot))
    {
        std::error_code ec;
        m_assetsRoot = std::filesystem::weakly_canonical(assetsRoot, ec);
        if (ec)
            m_assetsRoot = assetsRoot;
    }

    void remap(std::string& value)
    {
        if (value.empty() || value.rfind("builtin:", 0) == 0)
            return;

        std::filesystem::path p(value);
        if (!p.is_absolute())
            return;

        std::error_code ec;
        if (const std::filesystem::path canonical = std::filesystem::weakly_canonical(p, ec); !ec)
            p = canonical;

        if (!m_assetsRoot.empty() && pathStartsWith(p, m_assetsRoot)) {
            const std::filesystem::path rel = p.lexically_relative(m_assetsRoot);
            value = (std::filesystem::path("assets") / rel).generic_string();
            return;
        }

        // External file: copy it into the build's assets/_imported folder once.
        const std::string sourceKey = p.generic_string();
        if (auto it = m_copied.find(sourceKey); it != m_copied.end()) {
            value = it->second;
            return;
        }

        ec = {};
        if (!std::filesystem::exists(p, ec) || ec)
            return;  // missing on disk — leave the reference untouched

        const std::filesystem::path importRoot = m_buildAssetsRoot / "_imported";
        std::filesystem::create_directories(importRoot, ec);

        std::filesystem::path destination = importRoot / p.filename();
        for (int counter = 1; std::filesystem::exists(destination, ec); ++counter)
            destination = importRoot /
                (p.stem().string() + "_" + std::to_string(counter) + p.extension().string());

        ec = {};
        std::filesystem::copy_file(p, destination,
                                   std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            DEMON_LOG_WARN("ProjectCache: failed to copy external asset '{}' into the build.", sourceKey);
            return;
        }

        const std::string exported =
            (std::filesystem::path("assets/_imported") / destination.filename()).generic_string();
        m_copied.emplace(sourceKey, exported);
        value = exported;
    }

private:
    std::filesystem::path m_assetsRoot;
    std::filesystem::path m_buildAssetsRoot;
    std::map<std::string, std::string> m_copied;
};

void remapSceneAssetPaths(Scene& scene, ExportPathRemapper& remapper)
{
    for (EntityID id : scene.getEntities()) {
        if (auto* mr = scene.getComponent<MeshRendererComponent>(id)) {
            remapper.remap(mr->meshPath);
            remapper.remap(mr->materialPath);
        }
        if (auto* mc = scene.getComponent<MaterialComponent>(id)) {
            remapper.remap(mc->materialPath);
            remapper.remap(mc->albedoTexture);
            remapper.remap(mc->normalTexture);
            remapper.remap(mc->metallicTexture);
            remapper.remap(mc->emissiveTexture);
            mc->dirty = true;
        }
        if (auto* sky = scene.getComponent<SkyboxComponent>(id))
            remapper.remap(sky->texturePath);
        if (auto* light = scene.getComponent<LightComponent>(id))
            remapper.remap(light->cookieTexture);
        if (auto* foliage = scene.getComponent<TerrainFoliageComponent>(id)) {
            remapper.remap(foliage->treeMeshPath);
            remapper.remap(foliage->grassMeshPath);
        }
        if (auto* audio = scene.getComponent<AudioSourceComponent>(id))
            remapper.remap(audio->clipPath);
        if (auto* probe = scene.getComponent<ReflectionProbeComponent>(id))
            remapper.remap(probe->assetPath);
        if (auto* ui = scene.getComponent<UIElementComponent>(id))
            remapper.remap(ui->imagePath);
    }
}

bool exportPortableScene(const std::filesystem::path& sceneSource,
                         const std::filesystem::path& sceneDestination,
                         ExportPathRemapper& remapper)
{
    auto scene = Scene::create(sceneSource.stem().string());
    SceneSerializer serializer(scene);
    if (!serializer.deserialize(sceneSource.string()))
        return false;
    remapSceneAssetPaths(*scene, remapper);
    return serializer.serialize(sceneDestination.string());
}

} // namespace

std::filesystem::path ProjectCache::getLibraryRoot(const std::filesystem::path& projectRoot)
{
    return projectRoot / "Library";
}

std::filesystem::path ProjectCache::getShaderCacheRoot(const std::filesystem::path& projectRoot)
{
    return getLibraryRoot(projectRoot) / "ShaderCache";
}

std::filesystem::path ProjectCache::getAssetCacheRoot(const std::filesystem::path& projectRoot)
{
    return getLibraryRoot(projectRoot) / "AssetCache";
}

std::filesystem::path ProjectCache::getThumbnailCacheRoot(const std::filesystem::path& projectRoot)
{
    return getLibraryRoot(projectRoot) / "ThumbnailCache";
}

std::filesystem::path ProjectCache::getAssetDatabaseRoot(const std::filesystem::path& projectRoot)
{
    return getLibraryRoot(projectRoot) / "AssetDatabase";
}

bool ProjectCache::ensureLayout(const std::filesystem::path& projectRoot)
{
    if (projectRoot.empty())
        return false;

    std::error_code ec;
    std::filesystem::create_directories(getLibraryRoot(projectRoot), ec);
    std::filesystem::create_directories(getShaderCacheRoot(projectRoot), ec);
    std::filesystem::create_directories(getAssetCacheRoot(projectRoot), ec);
    std::filesystem::create_directories(getThumbnailCacheRoot(projectRoot), ec);
    std::filesystem::create_directories(getAssetDatabaseRoot(projectRoot), ec);
    std::filesystem::create_directories(projectRoot / "Saved" / "Logs", ec);
    std::filesystem::create_directories(projectRoot / "Temp", ec);
    return !ec;
}

bool ProjectCache::ensureProjectDatabases(const std::filesystem::path& projectRoot)
{
    if (!ensureLayout(projectRoot))
        return false;

    constexpr uint64_t MiB = 1024ull * 1024ull;
    const bool centralCache = reserveCacheFile(getAssetCacheRoot(projectRoot) / "demon_project_cache.bin",
                                               256ull * MiB);
    const bool thumbnailCache = reserveCacheFile(getThumbnailCacheRoot(projectRoot) / "demon_thumbs.bin",
                                                 128ull * MiB);
    const bool registry = reserveCacheFile(getAssetDatabaseRoot(projectRoot) / "demon_registry.bin",
                                           96ull * MiB);
    const bool index = reserveCacheFile(getAssetDatabaseRoot(projectRoot) / "demon_asset_index.bin",
                                        64ull * MiB);

    std::ofstream manifest(getAssetDatabaseRoot(projectRoot) / "asset_database_manifest.json",
                           std::ios::trunc);
    if (manifest.is_open()) {
        manifest << "{\n";
        manifest << "  \"format\": \"DemonAssetDatabaseIndex\",\n";
        manifest << "  \"version\": 1,\n";
        manifest << "  \"registry\": \"demon_registry.bin\",\n";
        manifest << "  \"index\": \"demon_asset_index.bin\",\n";
        manifest << "  \"thumbnailCache\": \"" << (getThumbnailCacheRoot(projectRoot) / "demon_thumbs.bin").string() << "\",\n";
        manifest << "  \"centralCache\": \"" << (getAssetCacheRoot(projectRoot) / "demon_project_cache.bin").string() << "\"\n";
        manifest << "}\n";
    }

    return centralCache && thumbnailCache && registry && index;
}

bool ProjectCache::compileShaders(const std::filesystem::path& projectRoot,
                                  const std::filesystem::path& shaderSourceDir,
                                  const std::filesystem::path& shaderCompilerExe)
{
    if (!ensureLayout(projectRoot))
        return false;

    const std::filesystem::path shaderOutputDir = getShaderCacheRoot(projectRoot);
    const std::filesystem::path absoluteShaderSourceDir = std::filesystem::absolute(shaderSourceDir);
    const std::filesystem::path absoluteShaderCompiler = std::filesystem::absolute(shaderCompilerExe);
    if (!absoluteShaderCompiler.empty()
        && std::filesystem::exists(absoluteShaderCompiler)
        && std::filesystem::exists(absoluteShaderSourceDir))
    {
        DWORD compilerExitCode = 0;
        if (runProcessAndWait(absoluteShaderCompiler,
                              projectRoot,
                              {L"--dir", absoluteShaderSourceDir.wstring(), L"--out", shaderOutputDir.wstring()},
                              compilerExitCode) &&
            compilerExitCode == 0)
        {
            return true;
        }

        DEMON_LOG_WARN("ProjectCache: shader compiler launch failed or returned {} for '{}'.",
                       static_cast<uint32_t>(compilerExitCode),
                       absoluteShaderCompiler.string());

        DEMON_LOG_ERROR("ProjectCache: shader compilation failed. Fix the HLSL/DXC diagnostics before packaging.");
        return false;
    }

    DEMON_LOG_WARN("ProjectCache: shader compiler/source unavailable, falling back to precompiled shader copy.");
    return copyCompiledShaders(absoluteShaderSourceDir.parent_path() / "dx12", shaderOutputDir);
}

bool ProjectCache::packAssets(const std::filesystem::path& assetsRoot,
                              const std::filesystem::path& outputPackage,
                              PackageStats* stats)
{
    return writePackageArchive(assetsRoot, outputPackage, "DMONPKG1", {}, stats);
}

bool ProjectCache::packCompiledShaders(const std::filesystem::path& shaderCacheRoot,
                                       const std::filesystem::path& outputPackage,
                                       PackageStats* stats)
{
    return writePackageArchive(shaderCacheRoot, outputPackage, "DMONDCS1", {".cso"}, stats);
}

bool ProjectCache::writeGlobalIlluminationBakeManifest(const std::filesystem::path& projectRoot,
                                                       std::string_view mode,
                                                       std::string_view projectName)
{
    if (!ensureLayout(projectRoot))
        return false;

    const std::filesystem::path bakeRoot = getLibraryRoot(projectRoot) / "GI";
    std::error_code ec;
    std::filesystem::create_directories(bakeRoot, ec);
    if (ec)
        return false;

    std::ofstream out(bakeRoot / "global_illumination_bake.json", std::ios::trunc);
    if (!out.is_open())
        return false;

    const auto timestamp = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    out << "{\n";
    out << "  \"project\": \"" << escapeJson(projectName) << "\",\n";
    out << "  \"mode\": \"" << escapeJson(mode) << "\",\n";
    out << "  \"generatedAt\": " << static_cast<long long>(timestamp) << ",\n";
    out << "  \"note\": \"Realtime uses DDGI probe updates. Baked locks a stable indirect profile until probe/lightmap texture baking is added.\"\n";
    out << "}\n";
    return true;
}

bool ProjectCache::buildWindowsPackage(const std::filesystem::path& projectRoot,
                                       const std::filesystem::path& assetsRoot,
                                       const std::filesystem::path& shaderSourceDir,
                                       const std::filesystem::path& shaderCompilerExe,
                                       const std::filesystem::path& currentExecutable,
                                       const ProjectSettings::BuildConfig& config,
                                       BuildPackageResult* result)
{
    if (result)
        *result = {};
    if (projectRoot.empty() || !ensureLayout(projectRoot))
        return false;

    std::error_code ec;
    const std::string outputName = sanitizeFileStem(config.outputName.empty() ? config.appName : config.outputName, "DemonGame");
    std::filesystem::path buildDirectory = config.buildDirectory.empty() ? std::filesystem::path("Build/Windows") : std::filesystem::path(config.buildDirectory);
    if (buildDirectory.is_absolute())
        buildDirectory = "Build/Windows";

    const std::filesystem::path absoluteProjectRoot = std::filesystem::weakly_canonical(projectRoot, ec);
    if (ec)
        return false;
    const std::filesystem::path buildRoot = projectRoot / buildDirectory / outputName;
    const std::filesystem::path absoluteBuildRoot = std::filesystem::weakly_canonical(buildRoot.parent_path(), ec) / buildRoot.filename();
    ec = {};
    if (!pathStartsWith(absoluteBuildRoot, absoluteProjectRoot) || absoluteBuildRoot == absoluteProjectRoot)
        return false;

    std::filesystem::remove_all(absoluteBuildRoot, ec);
    ec = {};
    std::filesystem::create_directories(absoluteBuildRoot, ec);
    if (ec)
        return false;

    BuildPackageResult localResult{};
    localResult.buildRoot = absoluteBuildRoot;
    localResult.dataRoot = absoluteBuildRoot / "Data";
    localResult.executablePath = absoluteBuildRoot / (outputName + ".exe");
    localResult.gameModulePath = absoluteBuildRoot / (outputName + ".dll");
    localResult.assetPackagePath = localResult.dataRoot / "bundles" / (outputName + ".demonpkg");
    localResult.shaderPackagePath = localResult.dataRoot / "visuals" / (outputName + ".dcs");
    localResult.manifestPath = localResult.dataRoot / "build_manifest.json";
    localResult.runtimeConfigPath = localResult.dataRoot / "runtime_config.json";
    localResult.looseAssetsRoot = absoluteBuildRoot / "assets";

    std::filesystem::create_directories(localResult.dataRoot / "bundles", ec);
    std::filesystem::create_directories(localResult.dataRoot / "visuals", ec);
    std::filesystem::create_directories(localResult.dataRoot / "Scenes", ec);
    if (ec)
        return false;

    const std::filesystem::path startupSceneSource =
        resolveStartupScenePath(projectRoot, assetsRoot, config.startupScene);
    if (startupSceneSource.empty() || !hasDemonSceneExtension(startupSceneSource)) {
        DEMON_LOG_ERROR("ProjectCache: startup scene '{}' is missing or is not a .demonscene file.",
                        config.startupScene);
        return false;
    }
    if (config.startupScene.empty())
        DEMON_LOG_INFO("ProjectCache: using startup scene '{}'.", startupSceneSource.string());

    localResult.startupScenePath =
        localResult.dataRoot / "Scenes" / sceneFileNameForExport(startupSceneSource, outputName);

    // Export the startup scene with asset paths rewritten to be relative to the
    // build root so the package runs on machines without the editor/project.
    ExportPathRemapper remapper(assetsRoot, localResult.looseAssetsRoot);
    if (!exportPortableScene(startupSceneSource, localResult.startupScenePath, remapper)) {
        DEMON_LOG_ERROR("ProjectCache: failed to export startup scene '{}'.",
                        startupSceneSource.string());
        return false;
    }

    if (config.compileShadersOnBuild) {
        if (!compileShaders(projectRoot, shaderSourceDir, shaderCompilerExe))
            return false;
    }

    if (config.copyExecutable && !currentExecutable.empty() && std::filesystem::exists(currentExecutable, ec)) {
        // Absolutize so parent_path() is valid even when a bare filename was given.
        const std::filesystem::path absoluteExecutable = std::filesystem::absolute(currentExecutable);
        std::filesystem::copy_file(absoluteExecutable,
                                   localResult.executablePath,
                                   std::filesystem::copy_options::overwrite_existing,
                                   ec);
        if (ec) {
            DEMON_LOG_ERROR("ProjectCache: failed to copy player executable '{}': {}",
                            absoluteExecutable.string(), ec.message());
            return false;
        }

        if (!copyRuntimeDlls(absoluteExecutable.parent_path(),
                             absoluteBuildRoot,
                             outputName,
                             &localResult.gameModulePath,
                             &localResult.runtimeDlls))
        {
            return false;
        }
    }

    if (!copyLooseAssets(assetsRoot, localResult.looseAssetsRoot)) {
        DEMON_LOG_ERROR("ProjectCache: copying loose assets '{}' -> '{}' failed.",
                        assetsRoot.string(), localResult.looseAssetsRoot.string());
        return false;
    }

    // Rewrite every copied .demonscene so absolute editor paths become
    // build-relative ones — runtime scene switching then works anywhere.
    if (std::filesystem::exists(localResult.looseAssetsRoot, ec)) {
        for (std::filesystem::recursive_directory_iterator it(
                 localResult.looseAssetsRoot,
                 std::filesystem::directory_options::skip_permission_denied, ec), end;
             it != end && !ec; it.increment(ec))
        {
            std::error_code entryEc;
            if (!it->is_regular_file(entryEc) || entryEc)
                continue;
            if (!hasDemonSceneExtension(it->path()))
                continue;
            if (!exportPortableScene(it->path(), it->path(), remapper))
                DEMON_LOG_WARN("ProjectCache: could not make scene portable: '{}'.",
                               it->path().string());
        }
        ec = {};
    }

    if (config.packageAssets) {
        if (!packAssets(assetsRoot, localResult.assetPackagePath, &localResult.assets)) {
            DEMON_LOG_ERROR("ProjectCache: packing assets from '{}' failed.", assetsRoot.string());
            return false;
        }
    }

    if (config.packageShaders) {
        if (!packCompiledShaders(getShaderCacheRoot(projectRoot), localResult.shaderPackagePath, &localResult.shaders)) {
            DEMON_LOG_ERROR("ProjectCache: packing compiled shaders from '{}' failed.",
                            getShaderCacheRoot(projectRoot).string());
            return false;
        }
    }

    std::ofstream manifest(localResult.manifestPath, std::ios::trunc);
    if (!manifest.is_open())
        return false;

    const std::string windowTitle = outputName.empty()
        ? (config.appName.empty() ? "DemonGame" : config.appName)
        : outputName;
    const std::string exportedSceneRelative =
        localResult.startupScenePath.lexically_relative(absoluteBuildRoot).generic_string();

    std::ofstream runtimeConfig(localResult.runtimeConfigPath, std::ios::trunc);
    if (!runtimeConfig.is_open())
        return false;
    runtimeConfig << "{\n";
    runtimeConfig << "  \"appName\": \"" << escapeJson(config.appName.empty() ? windowTitle : config.appName) << "\",\n";
    runtimeConfig << "  \"outputName\": \"" << escapeJson(outputName) << "\",\n";
    runtimeConfig << "  \"windowTitle\": \"" << escapeJson(windowTitle) << "\",\n";
    runtimeConfig << "  \"companyName\": \"" << escapeJson(config.companyName) << "\",\n";
    runtimeConfig << "  \"appVersion\": \"" << escapeJson(config.appVersion) << "\",\n";
    runtimeConfig << "  \"startupScene\": \"" << escapeJson(exportedSceneRelative) << "\",\n";
    runtimeConfig << "  \"dataRoot\": \"Data\",\n";
    runtimeConfig << "  \"looseAssetsRoot\": \"assets\",\n";
    runtimeConfig << "  \"assetPackage\": \"Data/bundles/" << escapeJson(localResult.assetPackagePath.filename().string()) << "\",\n";
    runtimeConfig << "  \"shaderPackage\": \"Data/visuals/" << escapeJson(localResult.shaderPackagePath.filename().string()) << "\"\n";
    runtimeConfig << "}\n";

    const auto timestamp = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    manifest << "{\n";
    manifest << "  \"appName\": \"" << escapeJson(config.appName) << "\",\n";
    manifest << "  \"outputName\": \"" << escapeJson(outputName) << "\",\n";
    manifest << "  \"windowTitle\": \"" << escapeJson(windowTitle) << "\",\n";
    manifest << "  \"companyName\": \"" << escapeJson(config.companyName) << "\",\n";
    manifest << "  \"appVersion\": \"" << escapeJson(config.appVersion) << "\",\n";
    manifest << "  \"bundleId\": \"" << escapeJson(config.bundleId) << "\",\n";
    manifest << "  \"platform\": \"Windows\",\n";
    manifest << "  \"generatedAt\": " << static_cast<long long>(timestamp) << ",\n";
    manifest << "  \"startupScene\": \"" << escapeJson(exportedSceneRelative) << "\",\n";
    manifest << "  \"runtimeExecutable\": \"" << escapeJson(localResult.executablePath.filename().string()) << "\",\n";
    manifest << "  \"gameModule\": \"" << escapeJson(localResult.gameModulePath.filename().string()) << "\",\n";
    manifest << "  \"dataRoot\": \"Data\",\n";
    manifest << "  \"runtimeConfig\": \"Data/runtime_config.json\",\n";
    manifest << "  \"looseAssetsRoot\": \"assets\",\n";
    manifest << "  \"assetPackage\": \"Data/bundles/" << escapeJson(localResult.assetPackagePath.filename().string()) << "\",\n";
    manifest << "  \"assetFiles\": " << localResult.assets.fileCount << ",\n";
    manifest << "  \"assetBytes\": " << localResult.assets.byteCount << ",\n";
    manifest << "  \"shaderPackage\": \"Data/visuals/" << escapeJson(localResult.shaderPackagePath.filename().string()) << "\",\n";
    manifest << "  \"shaderFiles\": " << localResult.shaders.fileCount << ",\n";
    manifest << "  \"shaderBytes\": " << localResult.shaders.byteCount << ",\n";
    manifest << "  \"runtimeDlls\": [";
    for (size_t i = 0; i < localResult.runtimeDlls.size(); ++i) {
        if (i > 0)
            manifest << ", ";
        manifest << "\"" << escapeJson(localResult.runtimeDlls[i].filename().string()) << "\"";
    }
    manifest << "]\n";
    manifest << "}\n";

    if (result)
        *result = localResult;
    return true;
}

bool ProjectCache::writeSceneAssetManifest(const std::filesystem::path& projectRoot, const Scene& scene)
{
    if (!ensureLayout(projectRoot))
        return false;

    std::set<std::string> meshes;
    std::set<std::string> materials;
    std::set<std::string> textures;
    std::set<std::string> skyboxes;

    for (EntityID id : scene.getEntities()) {
        if (const auto* mesh = scene.getComponent<MeshRendererComponent>(id)) {
            if (!mesh->meshPath.empty())
                meshes.insert(mesh->meshPath);
            if (!mesh->materialPath.empty())
                materials.insert(mesh->materialPath);
        }

        if (const auto* material = scene.getComponent<MaterialComponent>(id)) {
            if (!material->materialPath.empty())
                materials.insert(material->materialPath);
            if (!material->albedoTexture.empty()) textures.insert(material->albedoTexture);
            if (!material->normalTexture.empty()) textures.insert(material->normalTexture);
            if (!material->metallicTexture.empty()) textures.insert(material->metallicTexture);
            if (!material->emissiveTexture.empty()) textures.insert(material->emissiveTexture);
        }

        if (const auto* skybox = scene.getComponent<SkyboxComponent>(id)) {
            if (!skybox->texturePath.empty())
                skyboxes.insert(skybox->texturePath);
        }
    }

    std::ofstream out(getAssetCacheRoot(projectRoot) / "scene_assets.json", std::ios::trunc);
    if (!out.is_open())
        return false;

    auto writeArray = [&out](const char* key, const std::set<std::string>& values, bool trailingComma) {
        out << "  \"" << key << "\": [\n";
        bool first = true;
        for (const auto& value : values) {
            if (!first)
                out << ",\n";
            out << "    \"" << value << "\"";
            first = false;
        }
        out << "\n  ]";
        if (trailingComma)
            out << ",";
        out << "\n";
    };

    out << "{\n";
    writeArray("meshes", meshes, true);
    writeArray("materials", materials, true);
    writeArray("textures", textures, true);
    writeArray("skyboxes", skyboxes, false);
    out << "}\n";
    return true;
}

bool ProjectCache::writeCacheManifest(const std::filesystem::path& projectRoot, std::string_view projectName)
{
    if (!ensureLayout(projectRoot))
        return false;

    std::ofstream out(getLibraryRoot(projectRoot) / "cache_manifest.json", std::ios::trunc);
    if (!out.is_open())
        return false;

    const auto timestamp = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    out << "{\n";
    out << "  \"project\": \"" << projectName << "\",\n";
    out << "  \"generatedAt\": " << static_cast<long long>(timestamp) << ",\n";
    out << "  \"shaderCache\": \"" << getShaderCacheRoot(projectRoot).string() << "\",\n";
    out << "  \"assetCache\": \"" << getAssetCacheRoot(projectRoot).string() << "\",\n";
    out << "  \"thumbnailCache\": \"" << getThumbnailCacheRoot(projectRoot).string() << "\",\n";
    out << "  \"assetDatabase\": \"" << getAssetDatabaseRoot(projectRoot).string() << "\"\n";
    out << "}\n";
    return true;
}

} // namespace Demon
