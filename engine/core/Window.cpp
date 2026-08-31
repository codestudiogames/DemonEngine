// ==============================================================================
//  DemonEngine::Window  –  Win32 Implementation
// ==============================================================================
#include "Window.h"
#include "Logger.h"
#include "input/Input.h"
#include <windowsx.h>

namespace Demon {

namespace {

static bool s_classRegistered = false;
static const wchar_t* kWindowClass = L"DemonEngineWindow";
static constexpr WORD kAppIconResourceId = 101;

static HICON loadAppIcon(HINSTANCE instance, int width, int height)
{
    return static_cast<HICON>(LoadImageW(instance,
                                         MAKEINTRESOURCEW(kAppIconResourceId),
                                         IMAGE_ICON,
                                         width,
                                         height,
                                         LR_DEFAULTCOLOR));
}

static int modMask()
{
    int mods = 0;
    if (GetKeyState(VK_SHIFT)   & 0x8000) mods |= 0x0001;
    if (GetKeyState(VK_CONTROL) & 0x8000) mods |= 0x0002;
    if (GetKeyState(VK_MENU)    & 0x8000) mods |= 0x0004;
    if (GetKeyState(VK_LWIN)    & 0x8000) mods |= 0x0008;
    if (GetKeyState(VK_RWIN)    & 0x8000) mods |= 0x0008;
    return mods;
}

static int vkToKey(WPARAM vk)
{
    switch (vk) {
        case VK_ESCAPE:   return Key::Escape;
        case VK_RETURN:   return Key::Enter;
        case VK_TAB:      return Key::Tab;
        case VK_BACK:     return Key::Backspace;
        case VK_INSERT:   return Key::Insert;
        case VK_DELETE:   return Key::Delete;
        case VK_RIGHT:    return Key::Right;
        case VK_LEFT:     return Key::Left;
        case VK_DOWN:     return Key::Down;
        case VK_UP:       return Key::Up;
        case VK_PRIOR:    return Key::PageUp;
        case VK_NEXT:     return Key::PageDown;
        case VK_HOME:     return Key::Home;
        case VK_END:      return Key::End;
        case VK_F1:       return Key::F1;
        case VK_F2:       return Key::F2;
        case VK_F3:       return Key::F3;
        case VK_F4:       return Key::F4;
        case VK_F5:       return Key::F5;
        case VK_F6:       return Key::F6;
        case VK_F7:       return Key::F7;
        case VK_F8:       return Key::F8;
        case VK_F9:       return Key::F9;
        case VK_F10:      return Key::F10;
        case VK_F11:      return Key::F11;
        case VK_F12:      return Key::F12;
        case VK_LSHIFT:   return Key::LeftShift;
        case VK_RSHIFT:   return Key::RightShift;
        case VK_LCONTROL: return Key::LeftControl;
        case VK_RCONTROL: return Key::RightControl;
        case VK_LMENU:    return Key::LeftAlt;
        case VK_RMENU:    return Key::RightAlt;
        case VK_LWIN:     return Key::LeftSuper;
        case VK_RWIN:     return Key::RightSuper;
        case VK_SPACE:    return Key::Space;
        case VK_OEM_7:    return Key::Apostrophe;
        case VK_OEM_COMMA:return Key::Comma;
        case VK_OEM_MINUS:return Key::Minus;
        case VK_OEM_PERIOD:return Key::Period;
        case VK_OEM_2:    return Key::Slash;
        case VK_OEM_1:    return Key::Semicolon;
        case VK_OEM_PLUS: return Key::Equal;
        default: break;
    }

    if (vk >= '0' && vk <= '9')
        return static_cast<int>(vk);
    if (vk >= 'A' && vk <= 'Z')
        return static_cast<int>(vk);

    return -1;
}

static int messageToMouseButton(UINT msg)
{
    switch (msg) {
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP: return MouseButton::Left;
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP: return MouseButton::Right;
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP: return MouseButton::Middle;
        default: return -1;
    }
}

static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    Window* window = reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!window)
        return DefWindowProcW(hwnd, msg, wParam, lParam);

    return window->handleMessage(msg, wParam, lParam);
}

} // namespace

Window::Window(const WindowSpec& spec) {
    m_data.title      = spec.title;
    m_data.width      = spec.width;
    m_data.height     = spec.height;
    m_data.vsync      = spec.vsync;
    m_data.fullscreen = spec.fullscreen;
    m_data.resizable  = spec.resizable;
#ifdef DEMON_PLATFORM_WINDOWS
    if (spec.nativeHandle) {
        m_hwnd = spec.nativeHandle;
        m_ownsWindow = false;
        RECT client{};
        if (GetClientRect(m_hwnd, &client)) {
            m_data.width = static_cast<uint32_t>(client.right - client.left);
            m_data.height = static_cast<uint32_t>(client.bottom - client.top);
        }
        DEMON_LOG_INFO("Window attached to external Win32 viewport: {}x{}", m_data.width, m_data.height);
        return;
    }
#endif
    init();
}

Window::~Window() {
    if (m_hwnd && m_ownsWindow)
        DestroyWindow(m_hwnd);
    m_hwnd = nullptr;

    if (m_ownsWindow && s_classRegistered && m_instance) {
        UnregisterClassW(kWindowClass, m_instance);
        s_classRegistered = false;
    }
}

void Window::init() {
    m_instance = GetModuleHandleW(nullptr);

    if (!s_classRegistered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = m_instance;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hIcon = loadAppIcon(m_instance,
                               GetSystemMetrics(SM_CXICON),
                               GetSystemMetrics(SM_CYICON));
        wc.hIconSm = loadAppIcon(m_instance,
                                 GetSystemMetrics(SM_CXSMICON),
                                 GetSystemMetrics(SM_CYSMICON));
        if (!wc.hIcon)
            wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
        if (!wc.hIconSm)
            wc.hIconSm = LoadIcon(nullptr, IDI_APPLICATION);
        wc.lpszClassName = kWindowClass;
        RegisterClassExW(&wc);
        s_classRegistered = true;
    }

    DWORD style = WS_OVERLAPPEDWINDOW;
    if (!m_data.resizable) {
        style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    }

    RECT rect = { 0, 0, static_cast<LONG>(m_data.width), static_cast<LONG>(m_data.height) };
    AdjustWindowRect(&rect, style, FALSE);
    int winWidth = rect.right - rect.left;
    int winHeight = rect.bottom - rect.top;
    int x = CW_USEDEFAULT;
    int y = CW_USEDEFAULT;

    if (m_data.fullscreen) {
        style = WS_POPUP;
        winWidth = GetSystemMetrics(SM_CXSCREEN);
        winHeight = GetSystemMetrics(SM_CYSCREEN);
        x = 0;
        y = 0;
    }

    m_hwnd = CreateWindowExW(
        0, kWindowClass,
        std::wstring(m_data.title.begin(), m_data.title.end()).c_str(),
        style,
        x, y, winWidth, winHeight,
        nullptr, nullptr, m_instance, nullptr);

    DEMON_ASSERT(m_hwnd, "Failed to create Win32 window");
    SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    if (HICON bigIcon = loadAppIcon(m_instance, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON)))
        SendMessageW(m_hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(bigIcon));
    if (HICON smallIcon = loadAppIcon(m_instance, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON)))
        SendMessageW(m_hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(smallIcon));

    ShowWindow(m_hwnd, SW_SHOWDEFAULT);
    UpdateWindow(m_hwnd);

    RECT client{};
    if (GetClientRect(m_hwnd, &client)) {
        m_data.width = static_cast<uint32_t>(client.right - client.left);
        m_data.height = static_cast<uint32_t>(client.bottom - client.top);
    }

    DEMON_LOG_INFO("Window created: {}x{} — \"{}\"", m_data.width, m_data.height, m_data.title);
}

LRESULT Window::handleMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (m_nativeCallback) {
        if (auto result = m_nativeCallback(m_hwnd, msg, wParam, lParam))
            return *result;
    }

    auto dispatch = [this](Event& e) {
        if (m_data.callback)
            m_data.callback(e);
    };

    switch (msg) {
        case WM_CLOSE: {
            WindowCloseEvent e;
            dispatch(e);
            if (e.handled)
                return 0;
            m_shouldClose = true;
            DestroyWindow(m_hwnd);
            return 0;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        case WM_SIZE: {
            if (wParam == SIZE_MINIMIZED) {
                m_data.width = 0;
                m_data.height = 0;
                WindowResizeEvent e(0, 0);
                dispatch(e);
                return 0;
            }
            m_data.width  = static_cast<uint32_t>(LOWORD(lParam));
            m_data.height = static_cast<uint32_t>(HIWORD(lParam));
            WindowResizeEvent e(m_data.width, m_data.height);
            dispatch(e);
            return 0;
        }
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            int key = vkToKey(wParam);
            if (key != -1) {
                int repeat = (lParam & 0x40000000) ? 1 : 0;
                KeyPressedEvent e(key, modMask(), repeat);
                dispatch(e);
            }
            return 0;
        }
        case WM_KEYUP:
        case WM_SYSKEYUP: {
            int key = vkToKey(wParam);
            if (key != -1) {
                KeyReleasedEvent e(key, modMask());
                dispatch(e);
            }
            return 0;
        }
        case WM_CHAR: {
            KeyTypedEvent e(static_cast<unsigned int>(wParam));
            dispatch(e);
            return 0;
        }
        case WM_MOUSEMOVE: {
            const float x = static_cast<float>(GET_X_LPARAM(lParam));
            const float y = static_cast<float>(GET_Y_LPARAM(lParam));
            MouseMovedEvent e(x, y);
            dispatch(e);
            return 0;
        }
        case WM_MOUSEWHEEL: {
            const float delta = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / WHEEL_DELTA;
            Input::addScroll(delta);
            MouseScrolledEvent e(0.0f, delta);
            dispatch(e);
            return 0;
        }
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN: {
            int btn = messageToMouseButton(msg);
            if (btn != -1) {
                MouseButtonPressedEvent e(btn, modMask());
                dispatch(e);
            }
            return 0;
        }
        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP: {
            int btn = messageToMouseButton(msg);
            if (btn != -1) {
                MouseButtonReleasedEvent e(btn, modMask());
                dispatch(e);
            }
            return 0;
        }
        default:
            break;
    }

    return DefWindowProcW(m_hwnd, msg, wParam, lParam);
}

void Window::pollEvents() {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        if (msg.message == WM_QUIT)
            m_shouldClose = true;
    }

    if (!m_ownsWindow && m_hwnd) {
        if (!IsWindow(m_hwnd)) {
            m_shouldClose = true;
            return;
        }

        RECT client{};
        if (GetClientRect(m_hwnd, &client)) {
            const uint32_t width = static_cast<uint32_t>(std::max<LONG>(0, client.right - client.left));
            const uint32_t height = static_cast<uint32_t>(std::max<LONG>(0, client.bottom - client.top));
            if (width != m_data.width || height != m_data.height) {
                m_data.width = width;
                m_data.height = height;
                WindowResizeEvent e(width, height);
                if (m_data.callback)
                    m_data.callback(e);
            }
        }
    }
}

void Window::waitEvents() {
    MSG msg;
    if (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    } else {
        m_shouldClose = true;
    }
}

bool Window::shouldClose() const { return m_shouldClose; }

void Window::setVSync(bool enabled) {
    // Note: VSync is controlled via the DXGI swapchain present flags, not Win32.
    m_data.vsync = enabled;
}

void Window::setTitle(std::string_view title) {
    m_data.title = title;
    if (m_ownsWindow)
        SetWindowTextA(m_hwnd, m_data.title.c_str());
}

void Window::setBorderlessFullscreen(bool enabled)
{
    if (!m_hwnd || !m_ownsWindow || m_data.fullscreen == enabled)
        return;

    if (enabled) {
        m_windowedStyle = static_cast<DWORD>(GetWindowLongPtrW(m_hwnd, GWL_STYLE));
        m_windowedExStyle = static_cast<DWORD>(GetWindowLongPtrW(m_hwnd, GWL_EXSTYLE));
        m_windowedPlacement.length = sizeof(WINDOWPLACEMENT);
        m_savedWindowPlacement = GetWindowPlacement(m_hwnd, &m_windowedPlacement) == TRUE;

        MONITORINFO monitorInfo{};
        monitorInfo.cbSize = sizeof(MONITORINFO);
        if (!GetMonitorInfoW(MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST), &monitorInfo))
            return;

        SetWindowLongPtrW(m_hwnd, GWL_STYLE,
                          static_cast<LONG_PTR>((m_windowedStyle & ~WS_OVERLAPPEDWINDOW) | WS_POPUP));
        SetWindowLongPtrW(m_hwnd, GWL_EXSTYLE,
                          static_cast<LONG_PTR>(m_windowedExStyle &
                              ~(WS_EX_DLGMODALFRAME | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE)));
        SetWindowPos(m_hwnd,
                     HWND_TOP,
                     monitorInfo.rcMonitor.left,
                     monitorInfo.rcMonitor.top,
                     monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left,
                     monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top,
                     SWP_NOOWNERZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    } else {
        SetWindowLongPtrW(m_hwnd, GWL_STYLE, static_cast<LONG_PTR>(m_windowedStyle));
        SetWindowLongPtrW(m_hwnd, GWL_EXSTYLE, static_cast<LONG_PTR>(m_windowedExStyle));
        if (m_savedWindowPlacement)
            SetWindowPlacement(m_hwnd, &m_windowedPlacement);
        SetWindowPos(m_hwnd,
                     nullptr,
                     0,
                     0,
                     0,
                     0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                     SWP_NOOWNERZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        m_savedWindowPlacement = false;
    }

    m_data.fullscreen = enabled;
}

} // namespace Demon
