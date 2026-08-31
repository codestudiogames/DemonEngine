// ==============================================================================
//  DemonEngine::Input  –  Implementation
// ==============================================================================
#include "Input.h"

namespace Demon {

HWND Input::s_window = nullptr;
Input::State Input::s_current{};
Input::State Input::s_previous{};
bool Input::s_mouseLocked = false;
static float s_scrollAccum = 0.0f;

namespace {

void ensureCursorVisible()
{
    while (ShowCursor(TRUE) < 0) {
    }
}

void ensureCursorHidden()
{
    while (ShowCursor(FALSE) >= 0) {
    }
}

bool clientCenter(HWND window, POINT& clientCenter, POINT& screenCenter)
{
    RECT rect{};
    if (!GetClientRect(window, &rect))
        return false;

    clientCenter.x = (rect.right - rect.left) / 2;
    clientCenter.y = (rect.bottom - rect.top) / 2;
    screenCenter = clientCenter;
    return ClientToScreen(window, &screenCenter) != FALSE;
}

} // namespace

static int toVirtualKey(int keycode)
{
    switch (keycode) {
        case Key::Escape:      return VK_ESCAPE;
        case Key::Enter:       return VK_RETURN;
        case Key::Tab:         return VK_TAB;
        case Key::Backspace:   return VK_BACK;
        case Key::Insert:      return VK_INSERT;
        case Key::Delete:      return VK_DELETE;
        case Key::Right:       return VK_RIGHT;
        case Key::Left:        return VK_LEFT;
        case Key::Down:        return VK_DOWN;
        case Key::Up:          return VK_UP;
        case Key::PageUp:      return VK_PRIOR;
        case Key::PageDown:    return VK_NEXT;
        case Key::Home:        return VK_HOME;
        case Key::End:         return VK_END;
        case Key::F1:          return VK_F1;
        case Key::F2:          return VK_F2;
        case Key::F3:          return VK_F3;
        case Key::F4:          return VK_F4;
        case Key::F5:          return VK_F5;
        case Key::F6:          return VK_F6;
        case Key::F7:          return VK_F7;
        case Key::F8:          return VK_F8;
        case Key::F9:          return VK_F9;
        case Key::F10:         return VK_F10;
        case Key::F11:         return VK_F11;
        case Key::F12:         return VK_F12;
        case Key::LeftShift:   return VK_LSHIFT;
        case Key::RightShift:  return VK_RSHIFT;
        case Key::LeftControl: return VK_LCONTROL;
        case Key::RightControl:return VK_RCONTROL;
        case Key::LeftAlt:     return VK_LMENU;
        case Key::RightAlt:    return VK_RMENU;
        case Key::LeftSuper:   return VK_LWIN;
        case Key::RightSuper:  return VK_RWIN;
        case Key::Space:       return VK_SPACE;
        case Key::Apostrophe:  return VK_OEM_7;
        case Key::Comma:       return VK_OEM_COMMA;
        case Key::Minus:       return VK_OEM_MINUS;
        case Key::Period:      return VK_OEM_PERIOD;
        case Key::Slash:       return VK_OEM_2;
        case Key::Semicolon:   return VK_OEM_1;
        case Key::Equal:       return VK_OEM_PLUS;
        default: break;
    }

    if (keycode >= '0' && keycode <= '9')
        return keycode;
    if (keycode >= 'A' && keycode <= 'Z')
        return keycode;

    return 0;
}

bool Input::isKeyDown(int keycode) {
    if (keycode < 0 || keycode >= static_cast<int>(s_current.keys.size())) return false;
    return s_current.keys[static_cast<size_t>(keycode)];
}

bool Input::isKeyPressed(int keycode) {
    if (keycode < 0 || keycode >= static_cast<int>(s_current.keys.size())) return false;
    size_t idx = static_cast<size_t>(keycode);
    return s_current.keys[idx] && !s_previous.keys[idx];
}

bool Input::isKeyReleased(int keycode) {
    if (keycode < 0 || keycode >= static_cast<int>(s_current.keys.size())) return false;
    size_t idx = static_cast<size_t>(keycode);
    return !s_current.keys[idx] && s_previous.keys[idx];
}

bool Input::isMouseButtonDown(int button) {
    if (button < 0 || button >= static_cast<int>(s_current.mouseButtons.size())) return false;
    return s_current.mouseButtons[static_cast<size_t>(button)];
}

bool Input::isMouseButtonPressed(int button) {
    if (button < 0 || button >= static_cast<int>(s_current.mouseButtons.size())) return false;
    size_t idx = static_cast<size_t>(button);
    return s_current.mouseButtons[idx] && !s_previous.mouseButtons[idx];
}

bool Input::isMouseButtonReleased(int button) {
    if (button < 0 || button >= static_cast<int>(s_current.mouseButtons.size())) return false;
    size_t idx = static_cast<size_t>(button);
    return !s_current.mouseButtons[idx] && s_previous.mouseButtons[idx];
}

std::pair<float, float> Input::getMousePosition() {
    return { s_current.mouseX, s_current.mouseY };
}

float Input::getMouseX() { return s_current.mouseX; }
float Input::getMouseY() { return s_current.mouseY; }

std::pair<float, float> Input::getMouseDelta() {
    return { s_current.mouseDeltaX, s_current.mouseDeltaY };
}

float Input::getScrollDelta() { return s_current.scrollDelta; }

void Input::setMouseLocked(bool locked) {
    s_mouseLocked = locked;
    if (!s_window) return;
    if (locked) {
        RECT rect{};
        GetClientRect(s_window, &rect);
        POINT ul{ rect.left, rect.top };
        POINT lr{ rect.right, rect.bottom };
        ClientToScreen(s_window, &ul);
        ClientToScreen(s_window, &lr);
        RECT clip{ ul.x, ul.y, lr.x, lr.y };
        ClipCursor(&clip);

        POINT centerClient{};
        POINT centerScreen{};
        if (clientCenter(s_window, centerClient, centerScreen)) {
            SetCursorPos(centerScreen.x, centerScreen.y);
            s_current.mouseX = static_cast<float>(centerClient.x);
            s_current.mouseY = static_cast<float>(centerClient.y);
            s_previous.mouseX = s_current.mouseX;
            s_previous.mouseY = s_current.mouseY;
            s_current.lastMouseX = s_current.mouseX;
            s_current.lastMouseY = s_current.mouseY;
            s_current.mouseDeltaX = 0.0f;
            s_current.mouseDeltaY = 0.0f;
        }
        ensureCursorHidden();
    } else {
        ClipCursor(nullptr);
        ensureCursorVisible();
    }
}

bool Input::isMouseLocked() { return s_mouseLocked; }

bool Input::isGamepadConnected(int id) {
    (void)id;
    return false;
}

float Input::getGamepadAxis(int axis, int id) {
    (void)axis;
    (void)id;
    return 0.0f;
}

bool Input::isGamepadButtonDown(int button, int id) {
    (void)button;
    (void)id;
    return false;
}

void Input::init(HWND hwnd) {
    s_window = hwnd;
    s_current = {};
    s_previous = {};
    s_mouseLocked = false;
    s_scrollAccum = 0.0f;
    ClipCursor(nullptr);
    ensureCursorVisible();
}

void Input::addScroll(float delta) {
    s_scrollAccum += delta;
}

void Input::update() {
    if (!s_window) return;

    s_previous = s_current;
    s_current.scrollDelta = s_scrollAccum;
    s_current.mouseDeltaX = 0.0f;
    s_current.mouseDeltaY = 0.0f;
    s_scrollAccum = 0.0f;

    for (int k = 0; k < static_cast<int>(s_current.keys.size()); ++k) {
        int vk = toVirtualKey(k);
        if (vk == 0) {
            s_current.keys[static_cast<size_t>(k)] = false;
            continue;
        }
        s_current.keys[static_cast<size_t>(k)] = (GetAsyncKeyState(vk) & 0x8000) != 0;
    }

    s_current.mouseButtons[static_cast<size_t>(MouseButton::Left)] =
        (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    s_current.mouseButtons[static_cast<size_t>(MouseButton::Right)] =
        (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
    s_current.mouseButtons[static_cast<size_t>(MouseButton::Middle)] =
        (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;

    s_current.lastMouseX = s_previous.mouseX;
    s_current.lastMouseY = s_previous.mouseY;
    POINT p{};
    if (GetCursorPos(&p)) {
        if (s_mouseLocked) {
            POINT centerClient{};
            POINT centerScreen{};
            if (clientCenter(s_window, centerClient, centerScreen)) {
                s_current.mouseDeltaX = static_cast<float>(p.x - centerScreen.x);
                s_current.mouseDeltaY = static_cast<float>(p.y - centerScreen.y);
                s_current.mouseX = static_cast<float>(centerClient.x);
                s_current.mouseY = static_cast<float>(centerClient.y);
                SetCursorPos(centerScreen.x, centerScreen.y);
            }
        } else {
            ScreenToClient(s_window, &p);
            s_current.mouseX = static_cast<float>(p.x);
            s_current.mouseY = static_cast<float>(p.y);
            s_current.mouseDeltaX = s_current.mouseX - s_previous.mouseX;
            s_current.mouseDeltaY = s_current.mouseY - s_previous.mouseY;
        }
    }
}

} // namespace Demon
