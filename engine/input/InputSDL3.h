#pragma once
// ==============================================================================
//  DemonEngine::InputSDL3
//  SDL3-based input backend (keyboard, mouse, gamepad).
// ==============================================================================
#include "core/DemonPCH.h"

namespace Demon {

class InputSDL3 {
public:
    static bool init();
    static void shutdown();

    static void update();                          // polls SDL state
    static void handleEvent(void* sdlEvent);       // pass SDL_Event* if you already pump events

    static bool isKeyDown(int keycode);
    static bool isKeyPressed(int keycode);
    static bool isKeyReleased(int keycode);

    static bool  isMouseButtonDown(int button);
    static bool  isMouseButtonPressed(int button);
    static bool  isMouseButtonReleased(int button);
    static std::pair<float, float> getMousePosition();
    static std::pair<float, float> getMouseDelta();
    static float getScrollDelta();

    static bool  isGamepadConnected(int id = 0);
    static float getGamepadAxis(int axis, int id = 0);
    static bool  isGamepadButtonDown(int button, int id = 0);

private:
    struct State {
        std::array<bool, 512> keys{};
        std::array<bool, 8>   mouseButtons{};
        float mouseX{};
        float mouseY{};
        float mouseDeltaX{};
        float mouseDeltaY{};
        float scrollDelta{};
    };

    static void resetFrame();

    static State s_current;
    static State s_previous;
    static bool  s_initialized;
};

} // namespace Demon
