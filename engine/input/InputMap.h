#pragma once
// ==============================================================================
//  DemonEngine::InputMap
//  Action-based input binding.  Maps named actions → key/button codes.
//
//  Usage:
//      InputMap::bind("Jump",    Key::Space);
//      InputMap::bind("Attack",  MouseButton::Left);
//      if (InputMap::isActionPressed("Jump")) { ... }
// ==============================================================================
#include "core/DemonPCH.h"

namespace Demon {

enum class InputSource { Keyboard, MouseButton, GamepadButton, GamepadAxis };

struct InputBinding {
    InputSource source  = InputSource::Keyboard;
    int         code    = 0;
    int         gamepad = 0;      // gamepad id for gamepad sources
    float       deadzone = 0.15f; // for axis bindings
};

class InputMap {
public:
    // ── Bind actions ──────────────────────────────────────────────────────────
    static void bind  (const std::string& action, int keyCode);
    static void bindMouse  (const std::string& action, int button);
    static void bindGamepad(const std::string& action, int code,
                             InputSource src = InputSource::GamepadButton, int gamepadId = 0);
    static void unbind(const std::string& action);
    static void clear();

    // ── Query ─────────────────────────────────────────────────────────────────
    static bool  isActionDown    (const std::string& action);
    static bool  isActionPressed (const std::string& action);
    static bool  isActionReleased(const std::string& action);
    static float getActionAxis   (const std::string& action);   // -1..1

    // ── Save / Load ───────────────────────────────────────────────────────────
    static bool saveToFile  (const std::string& path);
    static bool loadFromFile(const std::string& path);

private:
    static std::unordered_map<std::string, InputBinding> s_bindings;
};

} // namespace Demon