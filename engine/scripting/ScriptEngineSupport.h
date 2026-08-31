#pragma once

#include "ScriptEngine.h"

namespace Demon::ScriptDetail {

using RuntimeErrorReporter = std::function<void(const ScriptCompiledStatement&, std::string_view reason)>;

[[nodiscard]] const ScriptEventBlock* selectEventBlock(const ScriptBehaviorDefinition& behavior,
                                                       std::string_view eventName);
void logCompileDiagnostic(const ScriptDiagnostic& diagnostic);
[[nodiscard]] std::string formatRuntimeStartDiagnostic(const ScriptDiagnostic& diagnostic);
[[nodiscard]] bool parseScriptFile(const std::filesystem::path& path,
                                   ScriptBehaviorDefinition& outBehavior,
                                   std::vector<ScriptDiagnostic>& outDiagnostics);
void executeEvent(Scene& scene,
                  ScriptComponent& script,
                  const ScriptBehaviorDefinition& behavior,
                  EntityID entityId,
                  std::string_view eventName,
                  float deltaTime,
                  std::string_view payload,
                  const ScriptEventBlock& block,
                  const RuntimeErrorReporter& reporter);

} // namespace Demon::ScriptDetail
