// ==============================================================================
//  DemonEngine::ProjectSettings
// ==============================================================================
#include "ProjectSettings.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <fstream>
#include <regex>
#include <sstream>
#include <unordered_map>

#include "Logger.h"
#include "serialization/Serialization.h"

namespace Demon {

namespace {

std::string readString(const JsonValue* value, std::string fallback = {})
{
    return (value && value->isString()) ? value->asString() : fallback;
}

bool readBool(const JsonValue* value, bool fallback)
{
    return value ? value->asBool(fallback) : fallback;
}

std::string trim(std::string_view text)
{
    size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])))
        ++start;
    size_t end = text.size();
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])))
        --end;
    return std::string(text.substr(start, end - start));
}

std::string stripQuotes(std::string value)
{
    value = trim(value);
    if (!value.empty() && value.back() == ';')
        value.pop_back();
    value = trim(value);
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
        return value.substr(1, value.size() - 2);
    return value;
}

bool readCodeBool(const std::unordered_map<std::string, std::string>& section,
                  std::string_view key,
                  bool fallback)
{
    const auto it = section.find(std::string(key));
    if (it == section.end())
        return fallback;
    const std::string value = trim(it->second);
    if (value == "true" || value == "1")
        return true;
    if (value == "false" || value == "0")
        return false;
    return fallback;
}

std::string readCodeString(const std::unordered_map<std::string, std::string>& section,
                           std::string_view key,
                           std::string fallback = {})
{
    const auto it = section.find(std::string(key));
    return it == section.end() ? fallback : stripQuotes(it->second);
}

struct DemonProjectCode {
    std::string name;
    std::unordered_map<std::string, std::string> root;
    std::unordered_map<std::string, std::string> build;
    std::unordered_map<std::string, std::string> scripting;
};

bool parseDemonProjectCode(std::string_view source,
                           DemonProjectCode& out,
                           std::string& error)
{
    static const std::regex headerRegex(R"DEM(^\s*demon_project\s+"([^"]+)"\s*\{\s*$)DEM");
    static const std::regex sectionRegex(R"(^\s*(build|scripting)\s*\{\s*$)");
    static const std::regex assignRegex(R"(^\s*(?:set\s+)?([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.+?)\s*;?\s*$)");

    std::istringstream stream{std::string(source)};
    std::string line;
    std::string section = "root";
    bool sawHeader = false;
    size_t lineNumber = 0;

    auto* target = &out.root;
    while (std::getline(stream, line)) {
        ++lineNumber;
        const size_t comment = line.find("//");
        if (comment != std::string::npos)
            line = line.substr(0, comment);
        line = trim(line);
        if (line.empty())
            continue;

        std::smatch match;
        if (!sawHeader) {
            if (!std::regex_match(line, match, headerRegex)) {
                error = std::format("line {}: expected 'demon_project \"Name\" {{'.", lineNumber);
                return false;
            }
            out.name = match[1].str();
            sawHeader = true;
            continue;
        }

        if (line == "}") {
            if (section == "root")
                break;
            section = "root";
            target = &out.root;
            continue;
        }

        if (section == "root" && std::regex_match(line, match, sectionRegex)) {
            section = match[1].str();
            target = section == "build" ? &out.build : &out.scripting;
            continue;
        }

        if (std::regex_match(line, match, assignRegex)) {
            (*target)[match[1].str()] = match[2].str();
            continue;
        }

        error = std::format("line {}: invalid Demon Project statement.", lineNumber);
        return false;
    }

    if (!sawHeader) {
        error = "missing demon_project header.";
        return false;
    }
    return true;
}

std::string quoteCode(std::string_view value)
{
    std::string out = "\"";
    for (const char ch : value) {
        if (ch == '\\' || ch == '"')
            out.push_back('\\');
        out.push_back(ch);
    }
    out.push_back('"');
    return out;
}

const char* boolCode(bool value)
{
    return value ? "true" : "false";
}

} // namespace

void ProjectSettings::applyProjectDefaults(std::string_view projectName)
{
    const std::string resolvedName = projectName.empty() ? "UntitledProject" : std::string(projectName);
    if (build.appName.empty())
        build.appName = resolvedName;
    if (build.outputName.empty())
        build.outputName = resolvedName;
    if (build.bundleId.empty()) {
        std::string sanitized = resolvedName;
        std::ranges::transform(sanitized, sanitized.begin(), [](unsigned char c) {
            if (std::isalnum(c))
                return static_cast<char>(std::tolower(c));
            return '_';
        });
        build.bundleId = std::format("com.demonengine.{}", sanitized);
    }
}

bool ProjectSettings::load(const std::filesystem::path& path, std::string_view fallbackProjectName)
{
    build = {};
    scripting = {};
    applyProjectDefaults(fallbackProjectName);

    if (path.empty() || !std::filesystem::exists(path))
        return true;

    std::ifstream in(path);
    if (!in.is_open()) {
        DEMON_LOG_ERROR("ProjectSettings: failed to open '{}'", path.string());
        return false;
    }

    std::stringstream buffer;
    buffer << in.rdbuf();
    const std::string source = buffer.str();

    if (path.extension() == ".demonproj") {
        DemonProjectCode document;
        std::string error;
        if (!parseDemonProjectCode(source, document, error)) {
            DEMON_LOG_ERROR("ProjectSettings: parse error in '{}': {}", path.string(), error);
            return false;
        }

        const std::string projectName = document.name.empty()
            ? std::string(fallbackProjectName)
            : document.name;
        applyProjectDefaults(projectName);
        build.appVersion = readCodeString(document.root, "version", build.appVersion);

        build.appName = readCodeString(document.build, "appName", build.appName);
        build.companyName = readCodeString(document.build, "companyName", build.companyName);
        build.appVersion = readCodeString(document.build, "appVersion", build.appVersion);
        build.outputName = readCodeString(document.build, "outputName", build.outputName);
        build.startupScene = readCodeString(document.build, "startupScene", build.startupScene);
        build.bundleId = readCodeString(document.build, "bundleId", build.bundleId);
        build.buildDirectory = readCodeString(document.build, "buildDirectory", build.buildDirectory);
        build.compileShadersOnBuild = readCodeBool(document.build, "compileShadersOnBuild", build.compileShadersOnBuild);
        build.packageAssets = readCodeBool(document.build, "packageAssets", build.packageAssets);
        build.packageShaders = readCodeBool(document.build, "packageShaders", build.packageShaders);
        build.copyExecutable = readCodeBool(document.build, "copyExecutable", build.copyExecutable);

        scripting.language = readCodeString(document.scripting, "language", scripting.language);
        scripting.extension = readCodeString(document.scripting, "extension", scripting.extension);
        scripting.idePath = readCodeString(document.scripting, "idePath", scripting.idePath.string());
        scripting.autoCompileOnFocus = readCodeBool(document.scripting, "autoCompileOnFocus", scripting.autoCompileOnFocus);
        scripting.hotReloadEnabled = readCodeBool(document.scripting, "hotReloadEnabled", scripting.hotReloadEnabled);

        applyProjectDefaults(projectName);
        return true;
    }

    JsonDocument document;
    if (!document.parse(source)) {
        DEMON_LOG_ERROR("ProjectSettings: parse error in '{}': {}", path.string(), document.error());
        return false;
    }

    const JsonValue& root = document.root();
    if (!root.isObject()) {
        DEMON_LOG_ERROR("ProjectSettings: invalid root object in '{}'", path.string());
        return false;
    }

    const std::string projectName = readString(root.find("name"), std::string(fallbackProjectName));
    applyProjectDefaults(projectName);

    build.appVersion = readString(root.find("version"), build.appVersion);

    if (const JsonValue* buildConfig = root.find("build"); buildConfig && buildConfig->isObject()) {
        build.appName = readString(buildConfig->find("appName"), build.appName);
        build.companyName = readString(buildConfig->find("companyName"), build.companyName);
        build.appVersion = readString(buildConfig->find("appVersion"), build.appVersion);
        build.outputName = readString(buildConfig->find("outputName"), build.outputName);
        build.startupScene = readString(buildConfig->find("startupScene"), build.startupScene);
        build.bundleId = readString(buildConfig->find("bundleId"), build.bundleId);
        build.buildDirectory = readString(buildConfig->find("buildDirectory"), build.buildDirectory);
        build.compileShadersOnBuild = readBool(buildConfig->find("compileShadersOnBuild"), build.compileShadersOnBuild);
        build.packageAssets = readBool(buildConfig->find("packageAssets"), build.packageAssets);
        build.packageShaders = readBool(buildConfig->find("packageShaders"), build.packageShaders);
        build.copyExecutable = readBool(buildConfig->find("copyExecutable"), build.copyExecutable);
    }

    if (const JsonValue* scriptingConfig = root.find("scripting"); scriptingConfig && scriptingConfig->isObject()) {
        scripting.language = readString(scriptingConfig->find("language"), scripting.language);
        scripting.extension = readString(scriptingConfig->find("extension"), scripting.extension);
        scripting.idePath = readString(scriptingConfig->find("idePath"), scripting.idePath.string());
        scripting.autoCompileOnFocus = readBool(scriptingConfig->find("autoCompileOnFocus"), scripting.autoCompileOnFocus);
        scripting.hotReloadEnabled = readBool(scriptingConfig->find("hotReloadEnabled"), scripting.hotReloadEnabled);
    }

    applyProjectDefaults(projectName);
    return true;
}

bool ProjectSettings::save(const std::filesystem::path& path) const
{
    if (path.empty())
        return false;

    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    if (path.extension() == ".demonproj") {
        std::ofstream out(path, std::ios::out | std::ios::trunc);
        if (!out.is_open()) {
            DEMON_LOG_ERROR("ProjectSettings: failed to write '{}'", path.string());
            return false;
        }

        out << "demon_project " << quoteCode(build.appName) << " {\n";
        out << "    set version = " << quoteCode(build.appVersion) << ";\n\n";
        out << "    build {\n";
        out << "        set appName = " << quoteCode(build.appName) << ";\n";
        out << "        set companyName = " << quoteCode(build.companyName) << ";\n";
        out << "        set appVersion = " << quoteCode(build.appVersion) << ";\n";
        out << "        set outputName = " << quoteCode(build.outputName) << ";\n";
        out << "        set startupScene = " << quoteCode(build.startupScene) << ";\n";
        out << "        set bundleId = " << quoteCode(build.bundleId) << ";\n";
        out << "        set buildDirectory = " << quoteCode(build.buildDirectory) << ";\n";
        out << "        set compileShadersOnBuild = " << boolCode(build.compileShadersOnBuild) << ";\n";
        out << "        set packageAssets = " << boolCode(build.packageAssets) << ";\n";
        out << "        set packageShaders = " << boolCode(build.packageShaders) << ";\n";
        out << "        set copyExecutable = " << boolCode(build.copyExecutable) << ";\n";
        out << "    }\n\n";
        out << "    scripting {\n";
        out << "        set language = " << quoteCode(scripting.language) << ";\n";
        out << "        set extension = " << quoteCode(scripting.extension) << ";\n";
        out << "        set idePath = " << quoteCode(scripting.idePath.string()) << ";\n";
        out << "        set autoCompileOnFocus = " << boolCode(scripting.autoCompileOnFocus) << ";\n";
        out << "        set hotReloadEnabled = " << boolCode(scripting.hotReloadEnabled) << ";\n";
        out << "    }\n";
        out << "}\n";
        return true;
    }

    JsonWriter writer(true, 2);
    writer.beginObject();
    writer.key("name");
    writer.value(build.appName);
    writer.key("version");
    writer.value(build.appVersion);

    writer.key("build");
    writer.beginObject();
    writer.key("appName");
    writer.value(build.appName);
    writer.key("companyName");
    writer.value(build.companyName);
    writer.key("appVersion");
    writer.value(build.appVersion);
    writer.key("outputName");
    writer.value(build.outputName);
    writer.key("startupScene");
    writer.value(build.startupScene);
    writer.key("bundleId");
    writer.value(build.bundleId);
    writer.key("buildDirectory");
    writer.value(build.buildDirectory);
    writer.key("compileShadersOnBuild");
    writer.value(build.compileShadersOnBuild);
    writer.key("packageAssets");
    writer.value(build.packageAssets);
    writer.key("packageShaders");
    writer.value(build.packageShaders);
    writer.key("copyExecutable");
    writer.value(build.copyExecutable);
    writer.endObject();

    writer.key("scripting");
    writer.beginObject();
    writer.key("language");
    writer.value(scripting.language);
    writer.key("extension");
    writer.value(scripting.extension);
    writer.key("idePath");
    writer.value(scripting.idePath.string());
    writer.key("autoCompileOnFocus");
    writer.value(scripting.autoCompileOnFocus);
    writer.key("hotReloadEnabled");
    writer.value(scripting.hotReloadEnabled);
    writer.endObject();
    writer.endObject();

    std::ofstream out(path, std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
        DEMON_LOG_ERROR("ProjectSettings: failed to write '{}'", path.string());
        return false;
    }

    out << writer.str();
    return true;
}

} // namespace Demon
