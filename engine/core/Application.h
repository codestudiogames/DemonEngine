#pragma once
// ==============================================================================
//  DemonEngine::Application  v1.2
//  The single entry point for every DemonEngine game or editor session.
// ==============================================================================
#include "DemonPCH.h"
#include "Window.h"
#include "EventSystem.h"
#include "Timer.h"
#include "LayerStack.h"
#include "ThreadingManager.h"
#include "RuntimeSettings.h"
#include "PlatformConfig.h"
#include "../renderer/Renderer.h"

namespace Demon {

struct ApplicationSpec {
    std::string name       = "DemonEngine App";
    uint32_t    width      = 1280;
    uint32_t    height     = 720;
    bool        fullscreen = false;
    bool        vsync      = true;
    bool        resizable  = true;
#ifdef DEMON_PLATFORM_WINDOWS
    HWND        nativeWindow = nullptr;
#endif
};

class Application {
public:
    explicit Application(ApplicationSpec spec = {});
    virtual ~Application();

    Application(const Application&)            = delete;
    Application& operator=(const Application&) = delete;

    void run();
    void close();
    void setPlatformEventPump(std::function<void()> pump) { m_platformEventPump = std::move(pump); }

    void pushLayer(std::unique_ptr<Layer> layer);
    void pushOverlay(std::unique_ptr<Layer> overlay);

    [[nodiscard]] Window&   getWindow()    const { return *m_window; }
    [[nodiscard]] Renderer& getRenderer()  const { return *m_renderer; }
    [[nodiscard]] LayerStack& getLayerStack() { return m_layerStack; }
    [[nodiscard]] const LayerStack& getLayerStack() const { return m_layerStack; }
    [[nodiscard]] float     getDeltaTime() const { return m_deltaTime; }
    [[nodiscard]] uint64_t  getFrameCount()const { return m_frameCount; }
    [[nodiscard]] float     getDpiScale()  const { return m_dpiScale; }

    static Application& get() { return *s_instance; }

private:
    void onWindowClose(const WindowCloseEvent& e);
    void onWindowResize(const WindowResizeEvent& e);
    void applyRuntimeSettings(bool initial);

    ApplicationSpec                     m_spec;
    std::unique_ptr<Window>             m_window;
    std::unique_ptr<Renderer>           m_renderer;
    LayerStack                          m_layerStack;
    Timer                               m_timer;
    ThreadingManager                    m_threading;
    RuntimeSettings                     m_runtimeSettings;
    PlatformConfig                      m_platformConfig;
    uint32_t                            m_threadStallWarnMs = 5000;
    uint32_t                            m_workerCount = 0;
    uint64_t                            m_workerAffinityMask = 0;

    bool     m_running    = true;
    bool     m_minimised  = false;
    uint32_t m_deferredResizeWidth = 0;
    uint32_t m_deferredResizeHeight = 0;
    float    m_deltaTime  = 0.0f;
    uint64_t m_frameCount = 0;
    float    m_dpiScale   = 1.0f;
    std::function<void()> m_platformEventPump;

    inline static Application* s_instance = nullptr;
};

} // namespace Demon

// ── Entry-point macro: place DEMON_CREATE_APP(YourAppClass) in one .cpp ──────
#define DEMON_CREATE_APP(AppClass)                       \
    int main(int /*argc*/, char** /*argv*/) {            \
        AppClass* app = new AppClass();                  \
        app->run();                                      \
        delete app;                                      \
        return 0;                                        \
    }
