// ==============================================================================
//  DemonEngine — Shader Compiler Tool
//  Compiles HLSL shaders using DXC (DirectX Shader Compiler).
//
//  Usage:
//      DemonShaderCompiler [--watch] [--dir <shader_dir>] [--out <output_dir>]
//
//  Default:
//      --dir assets/shaders/hlsl
//      --out assets/shaders/dx12
//
//  Naming:
//      *_vs.hlsl  → vs_6_0  (entry: VSMain)
//      *_ps.hlsl  → ps_6_0  (entry: PSMain)
//      *_cs.hlsl  → cs_6_0  (entry: CSMain)
// ==============================================================================
#include "core/Logger.h"
#include "WorkerPipe.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <unordered_map>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;
using namespace Demon;

struct StageInfo {
    std::string profile;
    std::string entry;
};

static bool getStageFromFilename(const fs::path& src, StageInfo& out)
{
    auto name = src.filename().string();
    if (name.size() < 8) return false;
    if (name.ends_with("_vs.hlsl")) { out.profile = "vs_6_0"; out.entry = "VSMain"; return true; }
    if (name.ends_with("_ps.hlsl")) { out.profile = "ps_6_0"; out.entry = "PSMain"; return true; }
    if (name.ends_with("_cs.hlsl")) { out.profile = "cs_6_0"; out.entry = "CSMain"; return true; }
    return false;
}

struct CompileResult { bool success; std::string output; std::string error; };

static fs::path executableDirectory()
{
#ifdef _WIN32
    std::vector<wchar_t> pathBuffer(32768);
    const DWORD pathLength = GetModuleFileNameW(nullptr,
                                                pathBuffer.data(),
                                                static_cast<DWORD>(pathBuffer.size()));
    if (pathLength > 0 && pathLength < pathBuffer.size())
        return fs::path(std::wstring(pathBuffer.data(), pathLength)).parent_path();
#endif
    return {};
}

static fs::path resolveDxcExecutable()
{
    if (const char* dxcEnv = std::getenv("DXC_PATH"); dxcEnv && *dxcEnv)
        return fs::path(dxcEnv);

    const fs::path bundledDxc = executableDirectory() / "dxc.exe";
    if (!bundledDxc.empty() && fs::exists(bundledDxc))
        return bundledDxc;

    return "dxc.exe";
}

static bool isIgnoredShaderPath(const fs::path& source, const fs::path& root)
{
    const fs::path relative = source.lexically_relative(root);
    for (const fs::path& component : relative) {
        const std::string name = component.string();
        if (!name.empty() && name.front() == '.')
            return true;
    }
    return false;
}

static std::string readTextFile(const fs::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) return {};

    return std::string(
        std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>()
    );
}

static bool containsEntryPoint(const std::string& source, std::string_view entry)
{
    if (entry.empty()) return false;

    const std::string pattern = std::string(entry) + "(";
    return source.find(pattern) != std::string::npos;
}

static std::string resolveEntryPoint(const fs::path& src, const StageInfo& stage)
{
    const std::string source = readTextFile(src);
    if (source.empty()) return stage.entry;

    if (containsEntryPoint(source, stage.entry)) {
        return stage.entry;
    }

    if (containsEntryPoint(source, "main")) {
        return "main";
    }

    return stage.entry;
}

CompileResult compileShader(const fs::path& src, const fs::path& outDir) {
    StageInfo stage;
    if (!getStageFromFilename(src, stage))
        return {false, "", std::format("Unknown stage naming: {}", src.string())};

    fs::create_directories(outDir);
    fs::path dst = outDir / (src.stem().string() + ".cso");
    const std::string entry = resolveEntryPoint(src, stage);

    const fs::path dxc = resolveDxcExecutable();

    std::string cmd = std::format(
    "cmd /C \"\"{}\" -T {} -E {} -O3 -Fo \"{}\" \"{}\"\"",
    dxc.string(),
    stage.profile,
    entry,
    dst.string(),
    src.string()
);

    int ret = std::system(cmd.c_str());
    if (ret != 0)
        return {false, "", std::format("dxc failed for '{}'", src.string())};

    return {true, dst.string(), ""};
}

int compileAll(const fs::path& inDir, const fs::path& outDir,
               int& compiled, int& failed) {
    compiled = failed = 0;
    for (auto& entry : fs::recursive_directory_iterator(inDir)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".hlsl") continue;
        if (isIgnoredShaderPath(entry.path(), inDir)) continue;

        StageInfo stage;
        if (!getStageFromFilename(entry.path(), stage)) {
            // Skip include/helper files (no stage suffix).
            continue;
        }

        auto res = compileShader(entry.path(), outDir);
        if (res.success) {
            DEMON_LOG_INFO("[Compiled] {} → {}", entry.path().string(), res.output);
            ++compiled;
        } else {
            DEMON_LOG_ERROR("[FAILED]   {}: {}", entry.path().string(), res.error);
            ++failed;
        }
    }
    return failed == 0 ? 0 : 1;
}

int main(int argc, char** argv) {
    Logger::init();
    DEMON_LOG_INFO("DemonEngine Shader Compiler v2.0 (DXC)");

    fs::path inDir  = "assets/shaders/hlsl";
    fs::path outDir = "assets/shaders/dx12";
    bool watch = false;
    bool workerMode = false;
    std::string workerName = "DemonShaderCompiler";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--worker")                       workerMode = true;
        else if ((arg == "--worker-name" || arg == "--name") && i+1 < argc) workerName = argv[++i];
        else if (arg == "--watch")                   watch  = true;
        else if (arg == "--dir"  && i+1 < argc)      inDir  = argv[++i];
        else if (arg == "--out"  && i+1 < argc)      outDir = argv[++i];
        else if (arg == "--help") {
            DEMON_LOG_INFO("Usage: DemonShaderCompiler [--worker] [--watch] [--dir <dir>] [--out <dir>]");
            return 0;
        }
    }

    if (workerMode) {
        DEMON_LOG_INFO("{} running in background named-pipe worker mode.", workerName);
        return Demon::WorkerIPC::runNamedPipeWorker(workerName, [](std::string_view command) {
            if (command == "COMPILE_PROGRESS")
                return std::string{"COMPILE_PROGRESS queueDepth=0 activeJobs=0"};
            return std::string{"SHADER_COMPILER_READY"};
        });
    }

    if (!fs::exists(inDir)) {
        DEMON_LOG_ERROR("Shader source directory not found: {}", inDir.string());
        return 1;
    }

    int compiled, failed;
    int ret = compileAll(inDir, outDir, compiled, failed);
    DEMON_LOG_INFO("Compiled: {}   Failed: {}", compiled, failed);

    if (!watch) return ret;

    DEMON_LOG_INFO("Watching {} for changes... (Ctrl+C to stop)", inDir.string());
    std::unordered_map<std::string, fs::file_time_type> mtimes;

    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        for (auto& entry : fs::recursive_directory_iterator(inDir)) {
            if (!entry.is_regular_file()) continue;
            if (entry.path().extension() != ".hlsl") continue;
            if (isIgnoredShaderPath(entry.path(), inDir)) continue;

            auto key   = entry.path().string();
            auto mtime = entry.last_write_time();
            if (mtimes[key] != mtime) {
                mtimes[key] = mtime;
                DEMON_LOG_INFO("Change detected: {}", key);
                auto res = compileShader(entry.path(), outDir);
                if (res.success) DEMON_LOG_INFO("OK → {}", res.output);
                else             DEMON_LOG_ERROR("Error: {}", res.error);
            }
        }
    }
}
