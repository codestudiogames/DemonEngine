// ==============================================================================
//  DemonEngine::InputSDL3
// ==============================================================================
#include "InputSDL3.h"
#include "Input.h"
#include "core/Logger.h"

#if defined(DEMON_USE_SDL3)
#include <SDL3/SDL.h>
#endif

namespace Demon {

InputSDL3::State InputSDL3::s_current{};
InputSDL3::State InputSDL3::s_previous{};
bool InputSDL3::s_initialized = false;

bool InputSDL3::init()
{
#if defined(DEMON_USE_SDL3)
    if (s_initialized)
        return true;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD) != 0) {
        DEMON_LOG_ERROR("SDL3 init failed: {}", SDL_GetError());
        return false;
    }

    s_current = {};
    s_previous = {};
    s_initialized = true;
    DEMON_LOG_INFO("InputSDL3: initialised.");
    return true;
#else
    DEMON_LOG_WARN("InputSDL3: DEMON_USE_SDL3 not enabled.");
    return false;
#endif
}

void InputSDL3::shutdown()
{
#if defined(DEMON_USE_SDL3)
    if (!s_initialized)
        return;

    SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    s_initialized = false;
    s_current = {};
    s_previous = {};
    DEMON_LOG_INFO("InputSDL3: shutdown.");
#endif
}

void InputSDL3::resetFrame()
{
    s_previous = s_current;
    s_current.scrollDelta = 0.0f;
    s_current.mouseDeltaX = 0.0f;
    s_current.mouseDeltaY = 0.0f;
}

void InputSDL3::update()
{
#if defined(DEMON_USE_SDL3)
    if (!s_initialized)
        return;

    resetFrame();

    SDL_Event e{};
    while (SDL_PollEvent(&e)) {
        handleEvent(&e);
    }

    const bool* keys = SDL_GetKeyboardState(nullptr);
    if (keys) {
        for (int i = 0; i < static_cast<int>(s_current.keys.size()); ++i)
            s_current.keys[static_cast<size_t>(i)] = false;

        auto setKey = [&](SDL_Scancode scan, int engineKey) {
            if (!engineKey || scan == SDL_SCANCODE_UNKNOWN)
                return;
            if (engineKey >= 0 && engineKey < static_cast<int>(s_current.keys.size()))
                s_current.keys[static_cast<size_t>(engineKey)] = keys[scan];
        };

        setKey(SDL_SCANCODE_ESCAPE, Key::Escape);
        setKey(SDL_SCANCODE_RETURN, Key::Enter);
        setKey(SDL_SCANCODE_TAB, Key::Tab);
        setKey(SDL_SCANCODE_BACKSPACE, Key::Backspace);
        setKey(SDL_SCANCODE_INSERT, Key::Insert);
        setKey(SDL_SCANCODE_DELETE, Key::Delete);
        setKey(SDL_SCANCODE_RIGHT, Key::Right);
        setKey(SDL_SCANCODE_LEFT, Key::Left);
        setKey(SDL_SCANCODE_DOWN, Key::Down);
        setKey(SDL_SCANCODE_UP, Key::Up);
        setKey(SDL_SCANCODE_PAGEUP, Key::PageUp);
        setKey(SDL_SCANCODE_PAGEDOWN, Key::PageDown);
        setKey(SDL_SCANCODE_HOME, Key::Home);
        setKey(SDL_SCANCODE_END, Key::End);
        setKey(SDL_SCANCODE_F1, Key::F1);
        setKey(SDL_SCANCODE_F2, Key::F2);
        setKey(SDL_SCANCODE_F3, Key::F3);
        setKey(SDL_SCANCODE_F4, Key::F4);
        setKey(SDL_SCANCODE_F5, Key::F5);
        setKey(SDL_SCANCODE_F6, Key::F6);
        setKey(SDL_SCANCODE_F7, Key::F7);
        setKey(SDL_SCANCODE_F8, Key::F8);
        setKey(SDL_SCANCODE_F9, Key::F9);
        setKey(SDL_SCANCODE_F10, Key::F10);
        setKey(SDL_SCANCODE_F11, Key::F11);
        setKey(SDL_SCANCODE_F12, Key::F12);
        setKey(SDL_SCANCODE_LSHIFT, Key::LeftShift);
        setKey(SDL_SCANCODE_RSHIFT, Key::RightShift);
        setKey(SDL_SCANCODE_LCTRL, Key::LeftControl);
        setKey(SDL_SCANCODE_RCTRL, Key::RightControl);
        setKey(SDL_SCANCODE_LALT, Key::LeftAlt);
        setKey(SDL_SCANCODE_RALT, Key::RightAlt);
        setKey(SDL_SCANCODE_LGUI, Key::LeftSuper);
        setKey(SDL_SCANCODE_RGUI, Key::RightSuper);
        setKey(SDL_SCANCODE_SPACE, Key::Space);
        setKey(SDL_SCANCODE_APOSTROPHE, Key::Apostrophe);
        setKey(SDL_SCANCODE_COMMA, Key::Comma);
        setKey(SDL_SCANCODE_MINUS, Key::Minus);
        setKey(SDL_SCANCODE_PERIOD, Key::Period);
        setKey(SDL_SCANCODE_SLASH, Key::Slash);
        setKey(SDL_SCANCODE_SEMICOLON, Key::Semicolon);
        setKey(SDL_SCANCODE_EQUALS, Key::Equal);

        for (char c = 'A'; c <= 'Z'; ++c) {
            SDL_Scancode sc = static_cast<SDL_Scancode>(SDL_SCANCODE_A + (c - 'A'));
            setKey(sc, static_cast<int>(c));
        }
        for (char c = '0'; c <= '9'; ++c) {
            SDL_Scancode sc = static_cast<SDL_Scancode>(SDL_SCANCODE_0 + (c - '0'));
            setKey(sc, static_cast<int>(c));
        }
    }

#endif
}

void InputSDL3::handleEvent(void* sdlEvent)
{
#if defined(DEMON_USE_SDL3)
    if (!s_initialized || !sdlEvent)
        return;

    const SDL_Event& e = *reinterpret_cast<SDL_Event*>(sdlEvent);
    switch (e.type) {
        case SDL_EVENT_MOUSE_MOTION:
            s_current.mouseX = static_cast<float>(e.motion.x);
            s_current.mouseY = static_cast<float>(e.motion.y);
            s_current.mouseDeltaX += static_cast<float>(e.motion.xrel);
            s_current.mouseDeltaY += static_cast<float>(e.motion.yrel);
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            const bool down = (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
            int button = 0;
            if (e.button.button == SDL_BUTTON_LEFT) button = MouseButton::Left;
            if (e.button.button == SDL_BUTTON_RIGHT) button = MouseButton::Right;
            if (e.button.button == SDL_BUTTON_MIDDLE) button = MouseButton::Middle;
            if (button >= 0 && button < static_cast<int>(s_current.mouseButtons.size()))
                s_current.mouseButtons[static_cast<size_t>(button)] = down;
            break;
        }
        case SDL_EVENT_MOUSE_WHEEL:
            s_current.scrollDelta += static_cast<float>(e.wheel.y);
            break;
        default:
            break;
    }
#else
    (void)sdlEvent;
#endif
}

bool InputSDL3::isKeyDown(int keycode)
{
    if (keycode < 0 || keycode >= static_cast<int>(s_current.keys.size()))
        return false;
    return s_current.keys[static_cast<size_t>(keycode)];
}

bool InputSDL3::isKeyPressed(int keycode)
{
    if (keycode < 0 || keycode >= static_cast<int>(s_current.keys.size()))
        return false;
    size_t idx = static_cast<size_t>(keycode);
    return s_current.keys[idx] && !s_previous.keys[idx];
}

bool InputSDL3::isKeyReleased(int keycode)
{
    if (keycode < 0 || keycode >= static_cast<int>(s_current.keys.size()))
        return false;
    size_t idx = static_cast<size_t>(keycode);
    return !s_current.keys[idx] && s_previous.keys[idx];
}

bool InputSDL3::isMouseButtonDown(int button)
{
    if (button < 0 || button >= static_cast<int>(s_current.mouseButtons.size()))
        return false;
    return s_current.mouseButtons[static_cast<size_t>(button)];
}

bool InputSDL3::isMouseButtonPressed(int button)
{
    if (button < 0 || button >= static_cast<int>(s_current.mouseButtons.size()))
        return false;
    size_t idx = static_cast<size_t>(button);
    return s_current.mouseButtons[idx] && !s_previous.mouseButtons[idx];
}

bool InputSDL3::isMouseButtonReleased(int button)
{
    if (button < 0 || button >= static_cast<int>(s_current.mouseButtons.size()))
        return false;
    size_t idx = static_cast<size_t>(button);
    return !s_current.mouseButtons[idx] && s_previous.mouseButtons[idx];
}

std::pair<float, float> InputSDL3::getMousePosition()
{
    return { s_current.mouseX, s_current.mouseY };
}

std::pair<float, float> InputSDL3::getMouseDelta()
{
    return { s_current.mouseDeltaX, s_current.mouseDeltaY };
}

float InputSDL3::getScrollDelta()
{
    return s_current.scrollDelta;
}

bool InputSDL3::isGamepadConnected(int id)
{
#if defined(DEMON_USE_SDL3)
    SDL_Gamepad* pad = SDL_GetGamepadFromPlayerIndex(id);
    return pad != nullptr;
#else
    (void)id;
    return false;
#endif
}

float InputSDL3::getGamepadAxis(int axis, int id)
{
#if defined(DEMON_USE_SDL3)
    SDL_Gamepad* pad = SDL_GetGamepadFromPlayerIndex(id);
    if (!pad)
        return 0.0f;
    return SDL_GetGamepadAxis(pad, static_cast<SDL_GamepadAxis>(axis)) / 32767.0f;
#else
    (void)axis;
    (void)id;
    return 0.0f;
#endif
}

bool InputSDL3::isGamepadButtonDown(int button, int id)
{
#if defined(DEMON_USE_SDL3)
    SDL_Gamepad* pad = SDL_GetGamepadFromPlayerIndex(id);
    if (!pad)
        return false;
    return SDL_GetGamepadButton(pad, static_cast<SDL_GamepadButton>(button)) == 1;
#else
    (void)button;
    (void)id;
    return false;
#endif
}

} // namespace Demon
