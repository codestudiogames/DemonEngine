#pragma once
// ==============================================================================
//  DemonEngine::ScriptEngine
//  Lightweight DemonScript registry, validation, hot reload, and runtime hooks.
// ==============================================================================
#include "core/DemonPCH.h"
#include "core/ProjectSettings.h"
#include "scene/Scene.h"
#include "physics/CollisionSystem.h"

namespace Demon {

struct ScriptFieldDefinition {
    std::string     name;
    ScriptFieldType type = ScriptFieldType::None;
    bool            hidden = false;
    ScriptFieldValue defaultValue;
};

struct ScriptDiagnostic {
    std::filesystem::path path;
    size_t                line = 0;
    size_t                column = 0;
    std::string           reason;
};

enum class ScriptStatementKind : uint8_t {
    Print = 0,
    Assign,
    VarDecl,
    IfStmt,
    FuncCall,
    Return,
};

enum class ScriptAssignOp : uint8_t {
    Set = 0,
    Add,
    Subtract,
    Multiply,
    Divide,
};

enum class ScriptTargetKind : uint8_t {
    None = 0,
    Field,
    TransformTranslationX,
    TransformTranslationY,
    TransformTranslationZ,
    TransformRotationX,
    TransformRotationY,
    TransformRotationZ,
    TransformScaleX,
    TransformScaleY,
    TransformScaleZ,
};

struct ScriptCompiledStatement {
    ScriptStatementKind kind = ScriptStatementKind::Print;
    ScriptAssignOp      assignOp = ScriptAssignOp::Set;
    ScriptTargetKind    targetKind = ScriptTargetKind::None;
    ScriptFieldType     valueType = ScriptFieldType::None;
    size_t              line = 0;
    size_t              column = 0;
    std::string         targetName;
    std::string         expression;
    std::vector<ScriptCompiledStatement> thenBlock;  // for if statements
    std::vector<ScriptCompiledStatement> elseBlock;  // for if statements
};

struct ScriptCompiledParameter {
    std::string name;
    ScriptFieldType type = ScriptFieldType::Float;
};

struct ScriptCompiledFunc {
    std::string                         name;
    std::vector<ScriptCompiledParameter> params;
    std::vector<ScriptCompiledStatement> body;
};

struct ScriptEventBlock {
    std::vector<ScriptCompiledStatement> statements;
};

struct ScriptBehaviorDefinition {
    std::string                 name;
    std::filesystem::path       path;
    std::filesystem::file_time_type lastWriteTime{};
    std::vector<ScriptFieldDefinition> fields;
    std::vector<ScriptCompiledFunc>    funcs;        // user-defined functions
    bool                        hasOnSpawn = false;
    bool                        hasOnTick = false;
    bool                        hasOnTrigger = false;
    bool                        hasOnSignal = false;
    std::string                 onSpawnBody;
    std::string                 onTickBody;
    std::string                 onTriggerBody;
    std::string                 onSignalBody;
    ScriptEventBlock            onSpawn;
    ScriptEventBlock            onTick;
    ScriptEventBlock            onTrigger;
    ScriptEventBlock            onSignal;
};

enum class DemonScriptValueKind : uint8_t {
    String = 0,
    Integer,
    Float,
    ResourcePath,
    Entity,
};

struct DemonScriptValue {
    DemonScriptValueKind kind = DemonScriptValueKind::String;
    std::string          text;
    int64_t              intValue = 0;
    float                floatValue = 0.0f;
    EntityID             entityId = NULL_ENTITY;
};

struct DemonScriptBinding {
    std::string alias;
    std::string entityName;
    EntityID    entityId = NULL_ENTITY;
    size_t      line = 0;
    size_t      column = 0;
};

struct DemonScriptCommand {
    std::string      actorAlias;
    EntityID         actorEntityId = NULL_ENTITY;
    std::string      action;
    DemonScriptValue value;
    size_t           line = 0;
    size_t           column = 0;
};

struct DemonScriptStoryDefinition {
    std::string                 name;
    std::filesystem::path       path;
    std::filesystem::file_time_type lastWriteTime{};
    std::vector<DemonScriptBinding> bindings;
    std::vector<DemonScriptCommand> commands;
};

struct ScriptCompileResult {
    bool success = true;
    size_t sourceCount = 0;
    size_t behaviorCount = 0;
    size_t storyCount = 0;
    size_t errorCount = 0;
};

class ScriptEngine {
public:
    static ScriptEngine& get();

    void configureProject(std::filesystem::path projectRoot,
                          std::filesystem::path assetsRoot,
                          ProjectSettings settings);

    [[nodiscard]] const ProjectSettings& getProjectSettings() const { return m_projectSettings; }
    [[nodiscard]] std::vector<std::string> getBehaviorNames() const;
    [[nodiscard]] std::vector<std::string> getStoryScriptPaths() const;
    [[nodiscard]] const ScriptBehaviorDefinition* findBehavior(std::string_view className) const;
    [[nodiscard]] const DemonScriptStoryDefinition* findStoryScript(std::string_view sourcePath) const;
    [[nodiscard]] const std::vector<ScriptDiagnostic>& getLastDiagnostics() const { return m_lastDiagnostics; }
    [[nodiscard]] bool hasSourceChanges() const;

    ScriptCompileResult rebuildRegistry(Scene* validationScene = nullptr);
    bool compileAndHotReload(Scene* editorScene = nullptr, Scene* runtimeScene = nullptr);
    void refreshComponent(ScriptComponent& component) const;
    void refreshScene(Scene& scene) const;

    bool openBehaviorInIde(std::string_view className) const;
    bool openSourceInIde(std::string_view sourcePath) const;
    void beginRuntime(Scene& scene);
    void endRuntime(Scene& scene);
    void updateScene(Scene& scene, float dt);
    void processCollisionPairs(Scene& scene, const std::vector<CollisionPair>& pairs);
    void sendSignal(Scene& scene, EntityID target, std::string_view signal);
    void sendSignalToAll(Scene& scene, std::string_view signal);

private:
    struct RuntimeSceneState {
        std::unordered_set<EntityID> spawnedEntities;
        std::unordered_set<uint64_t> activeTriggerPairs;
        std::unordered_set<uint64_t> reportedRuntimeErrors;
    };

    ScriptEngine() = default;

    [[nodiscard]] std::vector<std::filesystem::path> gatherScriptFiles() const;
    static bool parseScriptFile(const std::filesystem::path& path,
                                ScriptBehaviorDefinition& outBehavior,
                                std::vector<ScriptDiagnostic>& outDiagnostics);
    static bool parseStoryScriptFile(const std::filesystem::path& path,
                                     const Scene* validationScene,
                                     DemonScriptStoryDefinition& outStory,
                                     std::vector<ScriptDiagnostic>& outDiagnostics);
    [[nodiscard]] static std::optional<ScriptFieldValue> parseDefaultValue(const std::string& name,
                                                                           ScriptFieldType type,
                                                                           std::string_view expression,
                                                                           bool hidden);
    [[nodiscard]] static ScriptFieldType parseFieldType(std::string_view text);
    [[nodiscard]] static std::string trim(std::string_view text);
    [[nodiscard]] static std::string stripComment(std::string_view line);
    [[nodiscard]] static std::optional<std::string> extractBlock(std::string_view text, std::string_view token);
    static void logUnsupportedEvent(std::string_view eventName,
                                    const ScriptBehaviorDefinition& behavior,
                                    EntityID entityId);
    void writeRegistryCache() const;
    void ensureRuntimeState(Scene& scene);
    void executeStoryScript(Scene& scene,
                            const DemonScriptStoryDefinition& story,
                            EntityID ownerEntity);
    void dispatchEvent(Scene& scene,
                       std::string_view eventName,
                       const ScriptBehaviorDefinition& behavior,
                       EntityID entityId,
                       float deltaTime = 0.0f,
                       std::string_view payload = {});
    static uint64_t makeTriggerKey(EntityID a, EntityID b);
    [[nodiscard]] std::filesystem::path resolveSourcePath(std::string_view sourcePath) const;
    [[nodiscard]] std::string sourcePathKey(const std::filesystem::path& path) const;
    [[nodiscard]] std::string toProjectRelativePath(const std::filesystem::path& path) const;

    std::filesystem::path m_projectRoot;
    std::filesystem::path m_assetsRoot;
    ProjectSettings m_projectSettings;
    std::unordered_map<std::string, ScriptBehaviorDefinition> m_behaviors;
    std::unordered_map<std::string, DemonScriptStoryDefinition> m_storyScripts;
    std::unordered_map<const Scene*, RuntimeSceneState> m_runtimeStates;
    std::vector<ScriptDiagnostic> m_lastDiagnostics;
};

} // namespace Demon
