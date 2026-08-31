#pragma once
// ==============================================================================
//  DemonEngine::Window  –  Win32 native window
// ==============================================================================
#include "DemonPCH.h"
#include "EventSystem.h"

namespace Demon {

struct WindowSpec {
    std::string title      = "DemonEngine";
    uint32_t    width      = 1280;
    uint32_t    height     = 720;
    bool        fullscreen = false;
    bool        vsync      = true;
    bool        resizable  = true;
#ifdef DEMON_PLATFORM_WINDOWS
    HWND        nativeHandle = nullptr;
#endif
};

class Window {
public:
    using EventCallbackFn = std::function<void(Event&)>;
    using NativeMessageCallbackFn = std::function<std::optional<LRESULT>(HWND, UINT, WPARAM, LPARAM)>;

    explicit Window(const WindowSpec& spec);
    ~Window();

    void pollEvents();
    void waitEvents();         // efficient for editor (no active rendering)

    [[nodiscard]] uint32_t      getWidth()        const { return m_data.width; }
    [[nodiscard]] uint32_t      getHeight()       const { return m_data.height; }
    [[nodiscard]] bool          isVSync()         const { return m_data.vsync; }
    [[nodiscard]] bool          shouldClose()     const;
#ifdef DEMON_PLATFORM_WINDOWS
    [[nodiscard]] HWND          getWin32Handle()  const { return m_hwnd; }
#endif

    void setEventCallback(EventCallbackFn fn) { m_data.callback = std::move(fn); }
    void setNativeMessageCallback(NativeMessageCallbackFn fn) { m_nativeCallback = std::move(fn); }
    void setVSync(bool enabled);
    void setTitle(std::string_view title);
    void setBorderlessFullscreen(bool enabled);
    LRESULT handleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

private:
    void init();

    HWND m_hwnd = nullptr;
    HINSTANCE m_instance = nullptr;
    bool m_ownsWindow = true;
    bool m_shouldClose = false;
    bool m_savedWindowPlacement = false;
    DWORD m_windowedStyle = 0;
    DWORD m_windowedExStyle = 0;
    WINDOWPLACEMENT m_windowedPlacement{sizeof(WINDOWPLACEMENT)};
    NativeMessageCallbackFn m_nativeCallback;

    struct WindowData {
        std::string     title;
        uint32_t        width{}, height{};
        bool            vsync{};
        bool            fullscreen{};
        bool            resizable{};
        EventCallbackFn callback;
    } m_data;
};

} // namespace Demon
