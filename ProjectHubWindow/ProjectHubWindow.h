#pragma once
#include <filesystem>
#include <optional>
#include <string>

namespace Demon::ProjectHub {

enum class Action {
    Create,
    Open
};

struct Result {
    Action action = Action::Open;
    std::string name;
    std::filesystem::path projectDir;
    std::filesystem::path configPath;
};

std::optional<Result> show();

} // namespace Demon::ProjectHub

