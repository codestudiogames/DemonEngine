// ==============================================================================
//  DemonEngine::ScriptEngine
// ==============================================================================
#include "ScriptEngine.h"
#include "ScriptEngineSupport.h"

#include <charconv>
#include <cctype>
#include <limits>
#include <regex>
#include <sstream>

#include "core/Logger.h"
#include "scene/Scene.h"
#include "serialization/Serialization.h"

namespace Demon {

namespace {

bool launchProcess(const std::filesystem::path& executable, const std::filesystem::path& targetFile)
{
    if (executable.empty() || !std::filesystem::exists(executable))
        return false;

    std::wstring commandLine = std::format(L"\"{}\" \"{}\"",
                                           executable.wstring(),
                                           targetFile.wstring());
    std::vector<wchar_t> buffer(commandLine.begin(), commandLine.end());
    buffer.push_back(L'\0');

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo{};
    const BOOL created = CreateProcessW(executable.wstring().c_str(),
                                        buffer.data(),
                                        nullptr,
                                        nullptr,
                                        FALSE,
                                        0,
                                        nullptr,
                                        executable.parent_path().wstring().c_str(),
                                        &startupInfo,
                                        &processInfo);
    if (!created)
        return false;

    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    return true;
}

ScriptFieldValue makeDefaultFieldValue(const std::string& name, ScriptFieldType type, bool hidden)
{
    ScriptFieldValue value;
    value.name = name;
    value.type = type;
    value.hidden = hidden;
    return value;
}

std::string toLowerCopy(std::string_view text)
{
    std::string lower(text);
    std::ranges::transform(lower, lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return lower;
}

std::string trimStory(std::string_view text)
{
    size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])))
        ++start;
    size_t end = text.size();
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])))
        --end;
    return std::string(text.substr(start, end - start));
}

std::string stripStoryComment(std::string_view line)
{
    bool inQuote = false;
    for (size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '"')
            inQuote = !inQuote;
        if (!inQuote && i + 1 < line.size() && line[i] == '/' && line[i + 1] == '/')
            return trimStory(line.substr(0, i));
    }
    return trimStory(line);
}

size_t firstStoryColumn(std::string_view text)
{
    for (size_t i = 0; i < text.size(); ++i) {
        if (!std::isspace(static_cast<unsigned char>(text[i])))
            return i + 1;
    }
    return 1;
}

bool splitTrailingQuoted(std::string_view text,
                         std::string& outBefore,
                         std::string& outQuoted,
                         std::string& outAfter)
{
    const size_t open = text.find('"');
    if (open == std::string_view::npos)
        return false;
    const size_t close = text.rfind('"');
    if (close == open || close == std::string_view::npos)
        return false;

    outBefore = trimStory(text.substr(0, open));
    outQuoted = std::string(text.substr(open + 1, close - open - 1));
    outAfter = trimStory(text.substr(close + 1));
    return true;
}

bool parseIntLiteral(std::string_view text, int64_t& out)
{
    const std::string trimmed = trimStory(text);
    if (trimmed.empty())
        return false;
    const char* begin = trimmed.data();
    const char* end = begin + trimmed.size();
    auto [ptr, ec] = std::from_chars(begin, end, out);
    return ec == std::errc{} && ptr == end;
}

bool parseFloatLiteral(std::string_view text, float& out)
{
    const std::string trimmed = trimStory(text);
    if (trimmed.empty())
        return false;
    if (trimmed.find_first_of(".eE") == std::string::npos)
        return false;
    char* end = nullptr;
    out = std::strtof(trimmed.c_str(), &end);
    return end && *end == '\0';
}

bool looksLikeResourcePath(std::string_view text)
{
    const std::string value(text);
    if (value.find(":\\") != std::string::npos || value.find(":/") != std::string::npos)
        return true;
    if (value.starts_with("\\\\") || value.starts_with("//"))
        return true;
    if (value.find('\\') != std::string::npos || value.find('/') != std::string::npos)
        return true;

    const std::filesystem::path path(value);
    return path.has_extension();
}

bool actionUsesResourcePath(std::string_view action)
{
    const std::string lower = toLowerCopy(action);
    return lower.find("audio") != std::string::npos ||
           lower.find("sound") != std::string::npos ||
           lower.find("music") != std::string::npos ||
           lower.find("voice") != std::string::npos ||
           lower.find("clip") != std::string::npos ||
           lower.find("image") != std::string::npos ||
           lower.find("sprite") != std::string::npos ||
           lower.find("video") != std::string::npos ||
           lower.find("asset") != std::string::npos ||
           lower.find("path") != std::string::npos ||
           lower.find("file") != std::string::npos ||
           lower.find("resource") != std::string::npos;
}

bool actionRequiresEntityValue(std::string_view action)
{
    const std::string lower = toLowerCopy(action);
    return lower.find("_text") != std::string::npos ||
           lower.starts_with("text") ||
           lower.find("subtitle") != std::string::npos ||
           lower.find("entity") != std::string::npos ||
           lower.find("target") != std::string::npos ||
           lower.find("speaker") != std::string::npos ||
           lower.find("_ui") != std::string::npos;
}

EntityID findEntityByName(const Scene* scene, std::string_view name)
{
    if (!scene)
        return NULL_ENTITY;
    Entity entity = const_cast<Scene*>(scene)->getEntityByTag(name);
    return entity ? entity.getID() : NULL_ENTITY;
}

void addMissingEntityDiagnostic(std::vector<ScriptDiagnostic>& diagnostics,
                                const std::filesystem::path& path,
                                size_t line,
                                size_t column,
                                std::string_view entityName)
{
    diagnostics.push_back(ScriptDiagnostic{
        .path = path,
        .line = line,
        .column = column,
        .reason = std::format("ERR001 Couldn't found the entity name '{}' from EntityDatabase.",
                              entityName),
    });
}

bool classifyStoryValue(const std::filesystem::path& path,
                        const Scene* validationScene,
                        std::string_view action,
                        std::string_view rawValue,
                        size_t line,
                        size_t column,
                        DemonScriptValue& outValue,
                        std::vector<ScriptDiagnostic>& diagnostics)
{
    const std::string value = trimStory(rawValue);
    const bool requiresEntity = actionRequiresEntityValue(action);
    if (requiresEntity) {
        const EntityID entityId = findEntityByName(validationScene, value);
        if (validationScene && entityId == NULL_ENTITY) {
            addMissingEntityDiagnostic(diagnostics, path, line, column, value);
            return false;
        }
        outValue.kind = DemonScriptValueKind::Entity;
        outValue.text = value;
        outValue.entityId = entityId;
        return true;
    }

    if (actionUsesResourcePath(action) || looksLikeResourcePath(value)) {
        outValue.kind = DemonScriptValueKind::ResourcePath;
        outValue.text = value;
        return true;
    }

    if (const EntityID entityId = findEntityByName(validationScene, value); entityId != NULL_ENTITY) {
        outValue.kind = DemonScriptValueKind::Entity;
        outValue.text = value;
        outValue.entityId = entityId;
        return true;
    }

    int64_t intValue = 0;
    if (parseIntLiteral(value, intValue)) {
        outValue.kind = DemonScriptValueKind::Integer;
        outValue.text = value;
        outValue.intValue = intValue;
        return true;
    }

    float floatValue = 0.0f;
    if (parseFloatLiteral(value, floatValue)) {
        outValue.kind = DemonScriptValueKind::Float;
        outValue.text = value;
        outValue.floatValue = floatValue;
        return true;
    }

    outValue.kind = DemonScriptValueKind::String;
    outValue.text = value;
    return true;
}

std::string storyValueForLog(const DemonScriptValue& value)
{
    if (value.kind == DemonScriptValueKind::Entity)
        return std::format("{} ({})", value.text, value.entityId);
    return value.text;
}

} // namespace

ScriptEngine& ScriptEngine::get()
{
    static ScriptEngine instance;
    return instance;
}

void ScriptEngine::configureProject(std::filesystem::path projectRoot,
                                    std::filesystem::path assetsRoot,
                                    ProjectSettings settings)
{
    m_projectRoot = std::move(projectRoot);
    m_assetsRoot = std::move(assetsRoot);
    m_projectSettings = std::move(settings);
}

std::vector<std::string> ScriptEngine::getBehaviorNames() const
{
    std::vector<std::string> names;
    names.reserve(m_behaviors.size());
    for (const auto& [name, behavior] : m_behaviors) {
        (void)behavior;
        names.push_back(name);
    }
    std::ranges::sort(names);
    return names;
}

std::vector<std::string> ScriptEngine::getStoryScriptPaths() const
{
    std::vector<std::string> paths;
    paths.reserve(m_storyScripts.size());
    for (const auto& [key, story] : m_storyScripts) {
        (void)key;
        paths.push_back(toProjectRelativePath(story.path));
    }
    std::ranges::sort(paths);
    return paths;
}

const ScriptBehaviorDefinition* ScriptEngine::findBehavior(std::string_view className) const
{
    if (className.empty())
        return nullptr;
    auto it = m_behaviors.find(std::string(className));
    return it != m_behaviors.end() ? &it->second : nullptr;
}

const DemonScriptStoryDefinition* ScriptEngine::findStoryScript(std::string_view sourcePath) const
{
    if (sourcePath.empty())
        return nullptr;
    const std::string key = sourcePathKey(resolveSourcePath(sourcePath));
    auto it = m_storyScripts.find(key);
    return it != m_storyScripts.end() ? &it->second : nullptr;
}

bool ScriptEngine::hasSourceChanges() const
{
    const auto files = gatherScriptFiles();
    if (files.size() != m_behaviors.size() + m_storyScripts.size())
        return true;

    for (const auto& path : files) {
        std::error_code ec;
        const auto lastWrite = std::filesystem::last_write_time(path, ec);
        if (ec)
            return true;

        bool matched = false;
        for (const auto& [name, behavior] : m_behaviors) {
            (void)name;
            if (behavior.path == path) {
                matched = true;
                if (behavior.lastWriteTime != lastWrite)
                    return true;
                break;
            }
        }
        if (!matched) {
            const std::string key = sourcePathKey(path);
            auto storyIt = m_storyScripts.find(key);
            if (storyIt != m_storyScripts.end()) {
                matched = true;
                if (storyIt->second.lastWriteTime != lastWrite)
                    return true;
            }
        }
        if (!matched)
            return true;
    }

    return false;
}

ScriptCompileResult ScriptEngine::rebuildRegistry(Scene* validationScene)
{
    ScriptCompileResult result;
    const auto files = gatherScriptFiles();
    result.sourceCount = files.size();
    m_lastDiagnostics.clear();

    std::unordered_map<std::string, ScriptBehaviorDefinition> nextBehaviors;
    std::unordered_map<std::string, DemonScriptStoryDefinition> nextStories;
    for (const auto& path : files) {
        const std::string ext = toLowerCopy(path.extension().string());
        if (ext == ".ds") {
            DemonScriptStoryDefinition parsed;
            std::vector<ScriptDiagnostic> diagnostics;
            if (!parseStoryScriptFile(path, validationScene, parsed, diagnostics)) {
                result.success = false;
                for (ScriptDiagnostic& diagnostic : diagnostics)
                    m_lastDiagnostics.push_back(std::move(diagnostic));
                continue;
            }

            const std::string key = sourcePathKey(parsed.path);
            auto [it, inserted] = nextStories.emplace(key, std::move(parsed));
            if (!inserted) {
                m_lastDiagnostics.push_back(ScriptDiagnostic{
                    .path = path,
                    .line = 1,
                    .column = 1,
                    .reason = std::format("Duplicate DemonScript story file '{}'.",
                                          it->second.path.filename().string()),
                });
                result.success = false;
            }
        } else {
            ScriptBehaviorDefinition parsed;
            std::vector<ScriptDiagnostic> diagnostics;
            if (!parseScriptFile(path, parsed, diagnostics)) {
                result.success = false;
                for (ScriptDiagnostic& diagnostic : diagnostics)
                    m_lastDiagnostics.push_back(std::move(diagnostic));
                continue;
            }

            auto [it, inserted] = nextBehaviors.emplace(parsed.name, std::move(parsed));
            if (!inserted) {
                m_lastDiagnostics.push_back(ScriptDiagnostic{
                    .path = path,
                    .line = 1,
                    .column = 1,
                    .reason = std::format("Duplicate behavior '{}'. It is already defined in '{}'.",
                                          it->second.name,
                                          it->second.path.filename().string()),
                });
                result.success = false;
            }
        }
    }

    result.errorCount = m_lastDiagnostics.size();
    if (!result.success) {
        std::sort(m_lastDiagnostics.begin(),
                  m_lastDiagnostics.end(),
                  [](const ScriptDiagnostic& a, const ScriptDiagnostic& b) {
                      const auto keyA = std::tuple(a.path.string(), a.line, a.column, a.reason);
                      const auto keyB = std::tuple(b.path.string(), b.line, b.column, b.reason);
                      return keyA < keyB;
                  });

        for (const ScriptDiagnostic& diagnostic : m_lastDiagnostics)
            ScriptDetail::logCompileDiagnostic(diagnostic);
        return result;
    }

    m_behaviors = std::move(nextBehaviors);
    m_storyScripts = std::move(nextStories);
    result.behaviorCount = m_behaviors.size();
    result.storyCount = m_storyScripts.size();
    writeRegistryCache();

    DEMON_LOG_INFO("DemonScript: compiled {} behavior(s) and {} story/action script(s) from {} source file(s).",
                   result.behaviorCount,
                   result.storyCount,
                   result.sourceCount);
    return result;
}

bool ScriptEngine::compileAndHotReload(Scene* editorScene, Scene* runtimeScene)
{
    Scene* validationScene = editorScene ? editorScene : runtimeScene;
    const ScriptCompileResult result = rebuildRegistry(validationScene);
    if (!result.success)
        return false;

    if (editorScene)
        refreshScene(*editorScene);
    if (runtimeScene) {
        refreshScene(*runtimeScene);
        if (m_runtimeStates.contains(runtimeScene))
            beginRuntime(*runtimeScene);
    }

    DEMON_LOG_INFO("DemonScript: hot reload complete.");
    return true;
}

void ScriptEngine::refreshComponent(ScriptComponent& component) const
{
    if (component.attachmentKind != ScriptAttachmentKind::DemonScriptBehavior ||
        component.className.empty())
        return;

    const ScriptBehaviorDefinition* behavior = findBehavior(component.className);
    if (!behavior)
        return;

    std::vector<ScriptFieldValue> refreshed;
    refreshed.reserve(behavior->fields.size());
    for (const ScriptFieldDefinition& definition : behavior->fields) {
        auto existing = std::find_if(component.fieldValues.begin(),
                                     component.fieldValues.end(),
                                     [&](const ScriptFieldValue& value) {
                                         return value.name == definition.name &&
                                                value.type == definition.type;
                                     });

        if (existing != component.fieldValues.end()) {
            existing->hidden = definition.hidden;
            refreshed.push_back(*existing);
        } else {
            refreshed.push_back(definition.defaultValue);
        }
    }

    component.fieldValues = std::move(refreshed);
}

void ScriptEngine::refreshScene(Scene& scene) const
{
    for (auto [id, script] : scene.view<ScriptComponent>()) {
        (void)id;
        if (script && script->attachmentKind == ScriptAttachmentKind::DemonScriptBehavior)
            refreshComponent(*script);
    }
}

bool ScriptEngine::openBehaviorInIde(std::string_view className) const
{
    const ScriptBehaviorDefinition* behavior = findBehavior(className);
    if (!behavior) {
        DEMON_LOG_WARN("DemonScript: behavior '{}' was not found in the compiled registry.", className);
        return false;
    }

    if (m_projectSettings.scripting.idePath.empty() || !std::filesystem::exists(m_projectSettings.scripting.idePath)) {
        DEMON_LOG_WARN("DemonScript: IDE path is not configured. Open File > Project Settings and set Scripting & IDE.");
        return false;
    }

    if (!launchProcess(m_projectSettings.scripting.idePath, behavior->path)) {
        DEMON_LOG_ERROR("DemonScript: failed to launch IDE '{}' for '{}'.",
                        m_projectSettings.scripting.idePath.string(),
                        behavior->path.string());
        return false;
    }

    DEMON_LOG_INFO("DemonScript: opened '{}' in '{}'.",
                   behavior->path.filename().string(),
                   m_projectSettings.scripting.idePath.string());
    return true;
}

bool ScriptEngine::openSourceInIde(std::string_view sourcePath) const
{
    const std::filesystem::path resolved = resolveSourcePath(sourcePath);
    if (resolved.empty() || !std::filesystem::exists(resolved)) {
        DEMON_LOG_WARN("DemonScript: source file '{}' was not found.", sourcePath);
        return false;
    }

    if (m_projectSettings.scripting.idePath.empty() || !std::filesystem::exists(m_projectSettings.scripting.idePath)) {
        DEMON_LOG_WARN("DemonScript: IDE path is not configured. Open File > Project Settings and set Scripting & IDE.");
        return false;
    }

    if (!launchProcess(m_projectSettings.scripting.idePath, resolved)) {
        DEMON_LOG_ERROR("DemonScript: failed to launch IDE '{}' for '{}'.",
                        m_projectSettings.scripting.idePath.string(),
                        resolved.string());
        return false;
    }

    DEMON_LOG_INFO("DemonScript: opened '{}' in '{}'.",
                   resolved.filename().string(),
                   m_projectSettings.scripting.idePath.string());
    return true;
}

void ScriptEngine::beginRuntime(Scene& scene)
{
    RuntimeSceneState& state = m_runtimeStates[&scene];
    state.spawnedEntities.clear();
    state.activeTriggerPairs.clear();
    state.reportedRuntimeErrors.clear();

    refreshScene(scene);
    for (auto [id, script] : scene.view<ScriptComponent>()) {
        if (!script)
            continue;

        if (script->attachmentKind == ScriptAttachmentKind::DemonScriptFile) {
            if (!script->runOnStart || script->sourcePath.empty())
                continue;

            const DemonScriptStoryDefinition* story = findStoryScript(script->sourcePath);
            if (!story) {
                DEMON_LOG_ERROR("DemonScript: missing story/action script '{}' on entity {}.",
                                script->sourcePath,
                                id);
                continue;
            }

            executeStoryScript(scene, *story, id);
            continue;
        }

        if (script->attachmentKind != ScriptAttachmentKind::DemonScriptBehavior ||
            script->className.empty())
            continue;

        const ScriptBehaviorDefinition* behavior = findBehavior(script->className);
        if (!behavior) {
            DEMON_LOG_ERROR("DemonScript: missing behavior '{}' on entity {}.",
                            script->className,
                            id);
            continue;
        }

        state.spawnedEntities.insert(id);
        if (behavior->hasOnSpawn)
            dispatchEvent(scene, "on_spawn", *behavior, id);
    }
}

void ScriptEngine::endRuntime(Scene& scene)
{
    m_runtimeStates.erase(&scene);
}

void ScriptEngine::updateScene(Scene& scene, float dt)
{
    auto it = m_runtimeStates.find(&scene);
    if (it == m_runtimeStates.end())
        return;

    for (auto [id, script] : scene.view<ScriptComponent>()) {
        if (!script ||
            script->attachmentKind != ScriptAttachmentKind::DemonScriptBehavior ||
            script->className.empty())
            continue;

        const ScriptBehaviorDefinition* behavior = findBehavior(script->className);
        if (!behavior)
            continue;
        if (behavior->hasOnTick)
            dispatchEvent(scene, "on_update", *behavior, id, dt);
    }
}

void ScriptEngine::processCollisionPairs(Scene& scene, const std::vector<CollisionPair>& pairs)
{
    auto it = m_runtimeStates.find(&scene);
    if (it == m_runtimeStates.end())
        return;

    std::unordered_set<uint64_t> nextPairs;
    for (const CollisionPair& pair : pairs) {
        if (!pair.isTrigger)
            continue;

        const uint64_t key = makeTriggerKey(static_cast<EntityID>(pair.a), static_cast<EntityID>(pair.b));
        nextPairs.insert(key);
        if (it->second.activeTriggerPairs.contains(key))
            continue;

        if (auto* scriptA = scene.getComponent<ScriptComponent>(pair.a);
            scriptA &&
            scriptA->attachmentKind == ScriptAttachmentKind::DemonScriptBehavior &&
            !scriptA->className.empty()) {
            if (const ScriptBehaviorDefinition* behavior = findBehavior(scriptA->className); behavior && behavior->hasOnTrigger)
                dispatchEvent(scene, "on_trigger", *behavior, static_cast<EntityID>(pair.a), 0.0f, std::to_string(pair.b));
        }
        if (auto* scriptB = scene.getComponent<ScriptComponent>(pair.b);
            scriptB &&
            scriptB->attachmentKind == ScriptAttachmentKind::DemonScriptBehavior &&
            !scriptB->className.empty()) {
            if (const ScriptBehaviorDefinition* behavior = findBehavior(scriptB->className); behavior && behavior->hasOnTrigger)
                dispatchEvent(scene, "on_trigger", *behavior, static_cast<EntityID>(pair.b), 0.0f, std::to_string(pair.a));
        }
    }

    it->second.activeTriggerPairs = std::move(nextPairs);
}

void ScriptEngine::sendSignal(Scene& scene, EntityID target, std::string_view signal)
{
    if (auto* script = scene.getComponent<ScriptComponent>(target);
        script &&
        script->attachmentKind == ScriptAttachmentKind::DemonScriptBehavior &&
        !script->className.empty()) {
        if (const ScriptBehaviorDefinition* behavior = findBehavior(script->className); behavior && behavior->hasOnSignal)
            dispatchEvent(scene, "on_signal", *behavior, target, 0.0f, signal);
    }
}

void ScriptEngine::sendSignalToAll(Scene& scene, std::string_view signal)
{
    for (auto [id, script] : scene.view<ScriptComponent>()) {
        if (!script ||
            script->attachmentKind != ScriptAttachmentKind::DemonScriptBehavior ||
            script->className.empty())
            continue;
        const ScriptBehaviorDefinition* behavior = findBehavior(script->className);
        if (!behavior || !behavior->hasOnSignal)
            continue;
        dispatchEvent(scene, "on_signal", *behavior, id, 0.0f, signal);
    }
}

std::vector<std::filesystem::path> ScriptEngine::gatherScriptFiles() const
{
    std::vector<std::filesystem::path> files;
    const std::filesystem::path root = m_assetsRoot.empty()
        ? std::filesystem::absolute("assets")
        : m_assetsRoot;

    std::error_code ec;
    if (!std::filesystem::exists(root))
        return files;

    for (const auto& entry : std::filesystem::recursive_directory_iterator(root, ec)) {
        if (ec)
            break;
        if (!entry.is_regular_file())
            continue;
        std::string ext = toLowerCopy(entry.path().extension().string());
        std::string configuredExt = toLowerCopy(m_projectSettings.scripting.extension);
        if (!configuredExt.empty() && configuredExt.front() != '.')
            configuredExt.insert(configuredExt.begin(), '.');

        if (ext == ".demonscript" ||
            ext == ".ds" ||
            (!configuredExt.empty() && ext == configuredExt))
            files.push_back(entry.path());
    }

    std::ranges::sort(files);
    return files;
}

bool ScriptEngine::parseScriptFile(const std::filesystem::path& path,
                                   ScriptBehaviorDefinition& outBehavior,
                                   std::vector<ScriptDiagnostic>& outDiagnostics)
{
    return ScriptDetail::parseScriptFile(path, outBehavior, outDiagnostics);
}

bool ScriptEngine::parseStoryScriptFile(const std::filesystem::path& path,
                                        const Scene* validationScene,
                                        DemonScriptStoryDefinition& outStory,
                                        std::vector<ScriptDiagnostic>& outDiagnostics)
{
    std::ifstream in(path);
    if (!in.is_open()) {
        outDiagnostics.push_back(ScriptDiagnostic{
            .path = path,
            .line = 1,
            .column = 1,
            .reason = "Failed to open script file.",
        });
        return false;
    }

    std::vector<std::string> rawLines;
    std::string line;
    while (std::getline(in, line))
        rawLines.push_back(line);

    DemonScriptStoryDefinition story;
    story.name = path.stem().string();
    story.path = path;
    std::error_code ec;
    story.lastWriteTime = std::filesystem::last_write_time(path, ec);

    bool sawEnd = false;
    std::unordered_map<std::string, size_t> aliasToBinding;

    auto addDiagnostic = [&](size_t lineNumber, size_t column, std::string reason) {
        outDiagnostics.push_back(ScriptDiagnostic{
            .path = path,
            .line = lineNumber,
            .column = column,
            .reason = std::move(reason),
        });
    };

    for (size_t index = 0; index < rawLines.size(); ++index) {
        const size_t lineNumber = index + 1;
        const std::string trimmed = stripStoryComment(rawLines[index]);
        if (trimmed.empty())
            continue;

        const size_t column = firstStoryColumn(rawLines[index]);
        if (sawEnd) {
            addDiagnostic(lineNumber, column, "Unexpected statement after End.");
            continue;
        }

        if (trimmed == "End") {
            sawEnd = true;
            continue;
        }

        if (trimmed.starts_with("let ")) {
            std::string beforeQuote;
            std::string entityName;
            std::string afterQuote;
            if (!splitTrailingQuoted(trimmed.substr(4), beforeQuote, entityName, afterQuote) ||
                beforeQuote.empty() ||
                !afterQuote.empty()) {
                addDiagnostic(lineNumber,
                              column,
                              "Invalid DemonScript binding. Expected: let Alias \"EntityName\".");
                continue;
            }

            const EntityID entityId = findEntityByName(validationScene, entityName);
            if (validationScene && entityId == NULL_ENTITY) {
                addMissingEntityDiagnostic(outDiagnostics, path, lineNumber, column, entityName);
                continue;
            }

            if (aliasToBinding.contains(beforeQuote)) {
                addDiagnostic(lineNumber,
                              column,
                              std::format("Duplicate DemonScript alias '{}'.", beforeQuote));
                continue;
            }

            aliasToBinding.emplace(beforeQuote, story.bindings.size());
            story.bindings.push_back(DemonScriptBinding{
                .alias = beforeQuote,
                .entityName = entityName,
                .entityId = entityId,
                .line = lineNumber,
                .column = column,
            });
            continue;
        }

        std::string matchedAlias;
        size_t matchedBinding = std::numeric_limits<size_t>::max();
        for (const auto& [alias, bindingIndex] : aliasToBinding) {
            if (!trimmed.starts_with(alias))
                continue;
            if (trimmed.size() > alias.size() &&
                !std::isspace(static_cast<unsigned char>(trimmed[alias.size()])))
                continue;
            if (alias.size() <= matchedAlias.size())
                continue;
            matchedAlias = alias;
            matchedBinding = bindingIndex;
        }

        if (matchedBinding == std::numeric_limits<size_t>::max()) {
            addDiagnostic(lineNumber,
                          column,
                          "Unknown DemonScript actor alias. Declare it first with let Alias \"EntityName\".");
            continue;
        }

        const DemonScriptBinding& binding = story.bindings[matchedBinding];
        const std::string remainder = trim(trimmed.substr(matchedAlias.size()));
        if (remainder.empty()) {
            addDiagnostic(lineNumber, column, "DemonScript command is missing an action.");
            continue;
        }

        std::string action;
        std::string valueText;
        if (remainder.find('"') != std::string::npos) {
            std::string beforeQuote;
            std::string afterQuote;
            if (!splitTrailingQuoted(remainder, beforeQuote, valueText, afterQuote) ||
                beforeQuote.empty() ||
                !afterQuote.empty()) {
                addDiagnostic(lineNumber,
                              column,
                              "Invalid DemonScript command. Expected: Alias action \"value\".");
                continue;
            }

            std::istringstream actionStream(beforeQuote);
            actionStream >> action;
            std::string extra;
            if (actionStream >> extra) {
                addDiagnostic(lineNumber,
                              column,
                              "DemonScript command action must be a single token before the quoted value.");
                continue;
            }
        } else {
            std::istringstream commandStream(remainder);
            commandStream >> action;
            std::getline(commandStream, valueText);
            valueText = trim(valueText);
            if (valueText.empty()) {
                addDiagnostic(lineNumber,
                              column,
                              "DemonScript command is missing a value.");
                continue;
            }
        }

        DemonScriptValue value;
        if (!classifyStoryValue(path,
                                validationScene,
                                action,
                                valueText,
                                lineNumber,
                                column,
                                value,
                                outDiagnostics)) {
            continue;
        }

        story.commands.push_back(DemonScriptCommand{
            .actorAlias = binding.alias,
            .actorEntityId = binding.entityId,
            .action = action,
            .value = std::move(value),
            .line = lineNumber,
            .column = column,
        });
    }

    if (!sawEnd) {
        outDiagnostics.push_back(ScriptDiagnostic{
            .path = path,
            .line = rawLines.empty() ? 1 : rawLines.size(),
            .column = 1,
            .reason = "ERR002 Failed to end the script.",
        });
    }

    if (!outDiagnostics.empty())
        return false;

    outStory = std::move(story);
    return true;
}

std::optional<ScriptFieldValue> ScriptEngine::parseDefaultValue(const std::string& name,
                                                                ScriptFieldType type,
                                                                std::string_view expression,
                                                                bool hidden)
{
    ScriptFieldValue value = makeDefaultFieldValue(name, type, hidden);
    const std::string text = trim(expression);

    try {
        switch (type) {
            case ScriptFieldType::Bool:
                if (text == "true") value.boolValue = true;
                else if (text == "false") value.boolValue = false;
                else return std::nullopt;
                break;
            case ScriptFieldType::Int:
                value.intValue = std::stoll(text);
                break;
            case ScriptFieldType::Float:
                value.floatValue = std::stof(text);
                break;
            case ScriptFieldType::String:
                if (text.size() < 2 || text.front() != '"' || text.back() != '"')
                    return std::nullopt;
                value.stringValue = text.substr(1, text.size() - 2);
                break;
            case ScriptFieldType::Vec3: {
                if (text.size() < 5 || text.front() != '{' || text.back() != '}')
                    return std::nullopt;
                std::string inner = text.substr(1, text.size() - 2);
                std::replace(inner.begin(), inner.end(), ',', ' ');
                std::istringstream stream(inner);
                stream >> value.vec3Value.x >> value.vec3Value.y >> value.vec3Value.z;
                if (stream.fail())
                    return std::nullopt;
                break;
            }
            case ScriptFieldType::Entity:
            case ScriptFieldType::Entity3D:
            case ScriptFieldType::EntityImage:
            case ScriptFieldType::EntityUI:
                value.entityValue = text.empty() ? 0 : static_cast<uint64_t>(std::stoull(text));
                break;
            default:
                return std::nullopt;
        }
    } catch (...) {
        return std::nullopt;
    }

    return value;
}

ScriptFieldType ScriptEngine::parseFieldType(std::string_view text)
{
    const std::string lower = trim(text);
    if (lower == "bool")  return ScriptFieldType::Bool;
    if (lower == "int")   return ScriptFieldType::Int;
    if (lower == "float") return ScriptFieldType::Float;
    if (lower == "str" || lower == "string") return ScriptFieldType::String;
    if (lower == "vec3")  return ScriptFieldType::Vec3;
    if (lower == "entity") return ScriptFieldType::Entity;
    if (lower == "entity3d" || lower == "entity_3d") return ScriptFieldType::Entity3D;
    if (lower == "entityimg" || lower == "entity_img" || lower == "entity_image") return ScriptFieldType::EntityImage;
    if (lower == "entityui" || lower == "entity_ui") return ScriptFieldType::EntityUI;
    return ScriptFieldType::None;
}

std::string ScriptEngine::trim(std::string_view text)
{
    size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])))
        ++start;
    size_t end = text.size();
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])))
        --end;
    return std::string(text.substr(start, end - start));
}

std::string ScriptEngine::stripComment(std::string_view line)
{
    const size_t commentPos = line.find("//");
    return trim(line.substr(0, commentPos));
}

std::optional<std::string> ScriptEngine::extractBlock(std::string_view text, std::string_view token)
{
    const size_t tokenPos = text.find(token);
    if (tokenPos == std::string_view::npos)
        return std::nullopt;

    const size_t braceStart = text.find('{', tokenPos);
    if (braceStart == std::string_view::npos)
        return std::nullopt;

    int depth = 0;
    for (size_t i = braceStart; i < text.size(); ++i) {
        if (text[i] == '{')
            ++depth;
        else if (text[i] == '}') {
            --depth;
            if (depth == 0) {
                const size_t bodyStart = braceStart + 1;
                return std::string(text.substr(bodyStart, i - bodyStart));
            }
        }
    }

    return std::nullopt;
}

void ScriptEngine::logUnsupportedEvent(std::string_view eventName,
                                       const ScriptBehaviorDefinition& behavior,
                                       EntityID entityId)
{
    DEMON_LOG_INFO("DemonScript: {} fired for '{}' on entity {}. Full statement execution is not implemented yet.",
                   eventName,
                   behavior.name,
                   entityId);
}

void ScriptEngine::writeRegistryCache() const
{
    const std::filesystem::path projectRoot = m_projectRoot.empty()
        ? std::filesystem::current_path()
        : m_projectRoot;
    const std::filesystem::path cacheRoot = projectRoot / "Library" / "ScriptCache";

    std::error_code ec;
    std::filesystem::create_directories(cacheRoot, ec);

    JsonWriter writer(true, 2);
    writer.beginObject();
    writer.key("language");
    writer.value(m_projectSettings.scripting.language);
    writer.key("extension");
    writer.value(m_projectSettings.scripting.extension);
    writer.key("behaviors");
    writer.beginArray();
    std::vector<std::string> names = getBehaviorNames();
    for (const std::string& name : names) {
        const ScriptBehaviorDefinition* behavior = findBehavior(name);
        if (!behavior)
            continue;

        writer.beginObject();
        writer.key("name");
        writer.value(behavior->name);
        writer.key("path");
        writer.value(behavior->path.string());
        writer.key("onSpawn");
        writer.value(behavior->hasOnSpawn);
        writer.key("onTick");
        writer.value(behavior->hasOnTick);
        writer.key("onTrigger");
        writer.value(behavior->hasOnTrigger);
        writer.key("onSignal");
        writer.value(behavior->hasOnSignal);
        writer.key("fields");
        writer.beginArray();
        for (const ScriptFieldDefinition& field : behavior->fields) {
            writer.beginObject();
            writer.key("name");
            writer.value(field.name);
            writer.key("type");
            writer.value(static_cast<int64_t>(field.type));
            writer.key("hidden");
            writer.value(field.hidden);
            writer.endObject();
        }
        writer.endArray();
        writer.endObject();
    }
    writer.endArray();
    writer.key("storyScripts");
    writer.beginArray();
    std::vector<std::string> storyPaths = getStoryScriptPaths();
    for (const std::string& storyPath : storyPaths) {
        const DemonScriptStoryDefinition* story = findStoryScript(storyPath);
        if (!story)
            continue;

        writer.beginObject();
        writer.key("name");
        writer.value(story->name);
        writer.key("path");
        writer.value(toProjectRelativePath(story->path));
        writer.key("bindings");
        writer.beginArray();
        for (const DemonScriptBinding& binding : story->bindings) {
            writer.beginObject();
            writer.key("alias");
            writer.value(binding.alias);
            writer.key("entity");
            writer.value(binding.entityName);
            writer.key("entityId");
            writer.value(static_cast<uint64_t>(binding.entityId));
            writer.endObject();
        }
        writer.endArray();
        writer.key("commandCount");
        writer.value(static_cast<int64_t>(story->commands.size()));
        writer.endObject();
    }
    writer.endArray();
    writer.endObject();

    std::ofstream out(cacheRoot / "demonscript_registry.json", std::ios::out | std::ios::trunc);
    if (out.is_open())
        out << writer.str();
}

void ScriptEngine::ensureRuntimeState(Scene& scene)
{
    if (!m_runtimeStates.contains(&scene))
        beginRuntime(scene);
}

void ScriptEngine::executeStoryScript(Scene& scene,
                                      const DemonScriptStoryDefinition& story,
                                      EntityID ownerEntity)
{
    DEMON_LOG_INFO("[SCRIPT] DemonScript story '{}' started on entity {}.",
                   story.name,
                   ownerEntity);

    for (const DemonScriptCommand& command : story.commands) {
        if (command.actorEntityId == NULL_ENTITY || !scene.entityExists(command.actorEntityId)) {
            DEMON_LOG_ERROR("[RUNTIME]: DemonScript story '{}' command '{}' has a missing actor entity '{}'.",
                            story.name,
                            command.action,
                            command.actorAlias);
            continue;
        }

        const std::string actionLower = toLowerCopy(command.action);
        const bool isTextAction = actionLower.find("_text") != std::string::npos ||
                                  actionLower.starts_with("text") ||
                                  actionLower.find("subtitle") != std::string::npos;

        if (isTextAction) {
            if (command.value.kind == DemonScriptValueKind::Entity) {
                UIElementComponent* ui = scene.getComponent<UIElementComponent>(command.value.entityId);
                if (!ui) {
                    DEMON_LOG_WARN("[SCRIPT] DemonScript story '{}' resolved '{}' but it has no UI Element component.",
                                   story.name,
                                   command.value.text);
                    continue;
                }

                ui->visible = true;
                DEMON_LOG_INFO("[SCRIPT] {} {} -> UI entity {}.",
                               command.actorAlias,
                               command.action,
                               storyValueForLog(command.value));
                continue;
            }

            if (UIElementComponent* ui = scene.getComponent<UIElementComponent>(command.actorEntityId)) {
                ui->text = command.value.text;
                ui->visible = true;
                DEMON_LOG_INFO("[SCRIPT] {} {} -> \"{}\".",
                               command.actorAlias,
                               command.action,
                               command.value.text);
            } else {
                DEMON_LOG_WARN("[SCRIPT] DemonScript story '{}' text action '{}' needs a UI Element on '{}' or an entity value.",
                               story.name,
                               command.action,
                               command.actorAlias);
            }
            continue;
        }

        const bool isAudioAction = actionLower.find("audio") != std::string::npos ||
                                   actionLower.find("sound") != std::string::npos ||
                                   actionLower.find("voice") != std::string::npos ||
                                   actionLower.find("music") != std::string::npos;
        if (isAudioAction) {
            AudioSourceComponent* audio = scene.getComponent<AudioSourceComponent>(command.actorEntityId);
            if (!audio)
                audio = scene.addComponent(command.actorEntityId, AudioSourceComponent{});
            audio->clipPath = command.value.text;
            audio->playOnAwake = true;
            DEMON_LOG_INFO("[SCRIPT] {} {} -> {}.",
                           command.actorAlias,
                           command.action,
                           command.value.text);
            continue;
        }

        const bool isAnimationAction = actionLower.find("anim") != std::string::npos ||
                                       actionLower.find("motion") != std::string::npos;
        if (isAnimationAction) {
            AnimatorComponent* animator = scene.getComponent<AnimatorComponent>(command.actorEntityId);
            if (!animator)
                animator = scene.addComponent(command.actorEntityId, AnimatorComponent{});
            animator->currentClip = command.value.text;
            animator->playing = true;
            DEMON_LOG_INFO("[SCRIPT] {} {} -> {}.",
                           command.actorAlias,
                           command.action,
                           storyValueForLog(command.value));
            continue;
        }

        DEMON_LOG_INFO("[SCRIPT] {} {} -> {}.",
                       command.actorAlias,
                       command.action,
                       storyValueForLog(command.value));
    }
}

void ScriptEngine::dispatchEvent(Scene& scene,
                                 std::string_view eventName,
                                 const ScriptBehaviorDefinition& behavior,
                                 EntityID entityId,
                                 float deltaTime,
                                 std::string_view payload)
{
    ensureRuntimeState(scene);

    auto stateIt = m_runtimeStates.find(&scene);
    if (stateIt == m_runtimeStates.end())
        return;

    ScriptComponent* script = scene.getComponent<ScriptComponent>(entityId);
    if (!script ||
        script->attachmentKind != ScriptAttachmentKind::DemonScriptBehavior ||
        script->className.empty())
        return;

    const ScriptEventBlock* eventBlock = ScriptDetail::selectEventBlock(behavior, eventName);
    if (!eventBlock)
        return;

    ScriptDetail::executeEvent(scene,
                               *script,
                               behavior,
                               entityId,
                               eventName,
                               deltaTime,
                               payload,
                               *eventBlock,
                               [&](const ScriptCompiledStatement& statement, std::string_view reason) {
                                   const uint64_t runtimeKey = makeTriggerKey(entityId,
                                                                              static_cast<EntityID>((statement.line << 32u) ^ statement.column));
                                   if (!stateIt->second.reportedRuntimeErrors.insert(runtimeKey).second)
                                       return;

                                   DEMON_LOG_ERROR("[RUNTIME]: Script error in {} (line {}, col {}) on entity {}: {}",
                                                   behavior.path.filename().string(),
                                                   statement.line,
                                                   statement.column,
                                                   entityId,
                                                   reason);
                               });
}

uint64_t ScriptEngine::makeTriggerKey(EntityID a, EntityID b)
{
    if (a > b)
        std::swap(a, b);
    uint64_t seed = std::hash<uint64_t>{}(a);
    seed ^= std::hash<uint64_t>{}(b) + 0x9e3779b97f4a7c15ull + (seed << 6ull) + (seed >> 2ull);
    return seed;
}

std::filesystem::path ScriptEngine::resolveSourcePath(std::string_view sourcePath) const
{
    if (sourcePath.empty())
        return {};

    std::filesystem::path path{std::string(sourcePath)};
    std::error_code ec;
    if (path.is_absolute()) {
        const auto canonical = std::filesystem::weakly_canonical(path, ec);
        return ec ? path : canonical;
    }

    if (!m_assetsRoot.empty()) {
        const auto fromAssets = m_assetsRoot / path;
        if (std::filesystem::exists(fromAssets, ec)) {
            ec.clear();
            const auto canonical = std::filesystem::weakly_canonical(fromAssets, ec);
            return ec ? fromAssets : canonical;
        }
    }

    if (!m_projectRoot.empty()) {
        const auto fromProject = m_projectRoot / path;
        if (std::filesystem::exists(fromProject, ec)) {
            ec.clear();
            const auto canonical = std::filesystem::weakly_canonical(fromProject, ec);
            return ec ? fromProject : canonical;
        }
    }

    const auto absolute = std::filesystem::absolute(path, ec);
    return ec ? path : absolute;
}

std::string ScriptEngine::sourcePathKey(const std::filesystem::path& path) const
{
    std::error_code ec;
    std::filesystem::path normalized = std::filesystem::weakly_canonical(path, ec);
    if (ec)
        normalized = std::filesystem::absolute(path, ec);
    if (ec)
        normalized = path;

    std::string key = normalized.generic_string();
#ifdef _WIN32
    key = toLowerCopy(key);
#endif
    return key;
}

std::string ScriptEngine::toProjectRelativePath(const std::filesystem::path& path) const
{
    std::error_code ec;
    if (!m_assetsRoot.empty()) {
        auto relative = std::filesystem::relative(path, m_assetsRoot, ec);
        if (!ec && !relative.empty()) {
            const std::string rel = relative.generic_string();
            if (!rel.starts_with("../") && rel != "..")
                return rel;
        }
    }

    ec.clear();
    if (!m_projectRoot.empty()) {
        auto relative = std::filesystem::relative(path, m_projectRoot, ec);
        if (!ec && !relative.empty()) {
            const std::string rel = relative.generic_string();
            if (!rel.starts_with("../") && rel != "..")
                return rel;
        }
    }

    return path.string();
}

} // namespace Demon
