// ==============================================================================
//  DemonEngine::InputMap  –  Full Implementation
// ==============================================================================
#include "InputMap.h"
#include "Input.h"
#include "core/Logger.h"
#include <fstream>
#include <sstream>

namespace Demon {

std::unordered_map<std::string, InputBinding> InputMap::s_bindings;

// ── Bind ──────────────────────────────────────────────────────────────────────
void InputMap::bind(const std::string& action, int keyCode)
{
    InputBinding b;
    b.source = InputSource::Keyboard;
    b.code   = keyCode;
    s_bindings[action] = b;
}

void InputMap::bindMouse(const std::string& action, int button)
{
    InputBinding b;
    b.source = InputSource::MouseButton;
    b.code   = button;
    s_bindings[action] = b;
}

void InputMap::bindGamepad(const std::string& action, int code, InputSource src, int gamepadId)
{
    InputBinding b;
    b.source  = src;
    b.code    = code;
    b.gamepad = gamepadId;
    s_bindings[action] = b;
}

void InputMap::unbind(const std::string& action) { s_bindings.erase(action); }
void InputMap::clear()                           { s_bindings.clear(); }

// ── Query ─────────────────────────────────────────────────────────────────────
bool InputMap::isActionDown(const std::string& action)
{
    auto it = s_bindings.find(action);
    if (it == s_bindings.end()) return false;
    const auto& b = it->second;
    switch (b.source) {
        case InputSource::Keyboard:      return Input::isKeyDown(b.code);
        case InputSource::MouseButton:   return Input::isMouseButtonDown(b.code);
        case InputSource::GamepadButton: return Input::isGamepadButtonDown(b.code, b.gamepad);
        case InputSource::GamepadAxis:   return std::abs(Input::getGamepadAxis(b.code, b.gamepad)) > b.deadzone;
    }
    return false;
}

bool InputMap::isActionPressed(const std::string& action)
{
    auto it = s_bindings.find(action);
    if (it == s_bindings.end()) return false;
    const auto& b = it->second;
    switch (b.source) {
        case InputSource::Keyboard:    return Input::isKeyPressed(b.code);
        case InputSource::MouseButton: return Input::isMouseButtonPressed(b.code);
        default:                       return isActionDown(action);
    }
}

bool InputMap::isActionReleased(const std::string& action)
{
    auto it = s_bindings.find(action);
    if (it == s_bindings.end()) return false;
    const auto& b = it->second;
    switch (b.source) {
        case InputSource::Keyboard:    return Input::isKeyReleased(b.code);
        case InputSource::MouseButton: return Input::isMouseButtonReleased(b.code);
        default:                       return false;
    }
}

float InputMap::getActionAxis(const std::string& action)
{
    auto it = s_bindings.find(action);
    if (it == s_bindings.end()) return 0.0f;
    const auto& b = it->second;
    if (b.source == InputSource::GamepadAxis)
        return Input::getGamepadAxis(b.code, b.gamepad);
    return isActionDown(action) ? 1.0f : 0.0f;
}

// ── Save / Load ───────────────────────────────────────────────────────────────
bool InputMap::saveToFile(const std::string& path)
{
    std::ofstream f(path);
    if (!f) { DEMON_LOG_ERROR("InputMap: cannot write '{}'", path); return false; }
    for (auto& [action, b] : s_bindings)
        f << action << " " << static_cast<int>(b.source) << " " << b.code << " " << b.gamepad << "\n";
    DEMON_LOG_INFO("InputMap saved: '{}'", path);
    return true;
}

bool InputMap::loadFromFile(const std::string& path)
{
    std::ifstream f(path);
    if (!f) { DEMON_LOG_ERROR("InputMap: cannot open '{}'", path); return false; }
    s_bindings.clear();
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        std::string action; int src, code, gamepad;
        ss >> action >> src >> code >> gamepad;
        InputBinding b;
        b.source  = static_cast<InputSource>(src);
        b.code    = code;
        b.gamepad = gamepad;
        s_bindings[action] = b;
    }
    DEMON_LOG_INFO("InputMap loaded: '{}'", path);
    return true;
}

} // namespace Demon