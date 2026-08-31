#pragma once
// ==============================================================================
//  DemonEngine::Input
//  Static polling-based input.  Works anywhere in your game code:
//
//      if (Input::isKeyDown(Key::W)) moveForward();
//      auto [mx, my] = Input::getMousePosition();
// ==============================================================================
#include "core/DemonPCH.h"

namespace Demon {

// ── Key codes (mirrors GLFW) ───────────────────────────────────────────────────
namespace Key {
    enum Code : int {
        // Printable
        Space = 32, Apostrophe = 39, Comma = 44, Minus = 45, Period = 46, Slash = 47,
        D0 = 48, D1, D2, D3, D4, D5, D6, D7, D8, D9,
        Semicolon = 59, Equal = 61,
        A = 65, B, C, D, E, F, G, H, I, J, K, L, M,
        N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
        // Function / control
        Escape = 256, Enter, Tab, Backspace, Insert, Delete,
        Right, Left, Down, Up,
        PageUp, PageDown, Home, End,
        F1 = 290, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
        LeftShift = 340, LeftControl = 341, LeftAlt = 342, LeftSuper = 343,
        RightShift = 344, RightControl = 345, RightAlt = 346, RightSuper = 347,
    };
}

namespace MouseButton {
    enum Code : int { Left = 0, Right = 1, Middle = 2 };
}

// ─────────────────────────────────────────────────────────────────────────────
class Input {
public:
    // ── Keyboard ─────────────────────────────────────────────────────────────
    static bool isKeyDown(int keycode);       // held this frame
    static bool isKeyPressed(int keycode);    // just pressed (first frame only)
    static bool isKeyReleased(int keycode);   // just released

    // ── Mouse ─────────────────────────────────────────────────────────────────
    static bool  isMouseButtonDown(int button);
    static bool  isMouseButtonPressed(int button);
    static bool  isMouseButtonReleased(int button);

    static std::pair<float, float> getMousePosition();
    static float getMouseX();
    static float getMouseY();
    static std::pair<float, float> getMouseDelta();   // pixels moved this frame
    static float getScrollDelta();                    // y scroll this frame

    static void setMouseLocked(bool locked);          // hides + centres cursor
    static bool isMouseLocked();

    // ── Gamepad (basic) ───────────────────────────────────────────────────────
    static bool  isGamepadConnected(int id = 0);
    static float getGamepadAxis(int axis, int id = 0);
    static bool  isGamepadButtonDown(int button, int id = 0);

    // Internal — called by Window/Application
    static void init(HWND hwnd);
    static void update();                             // polls Win32 state
    static void addScroll(float delta);

private:
    static HWND s_window;
    struct State {
        std::array<bool, 512>  keys{};
        std::array<bool, 8>    mouseButtons{};
        float  mouseX{}, mouseY{};
        float  lastMouseX{}, lastMouseY{};
        float  mouseDeltaX{}, mouseDeltaY{};
        float  scrollDelta{};
    };
    static State s_current;
    static State s_previous;
    static bool  s_mouseLocked;
};

} // namespace Demon
