// ==============================================================================
//  DemonEngine::Application  –  Fixed Implementation
// ==============================================================================
#include "Application.h"
#include "Logger.h"
#include "input/Input.h"
#include <cctype>

namespace Demon {

namespace {

std::optional<LogLevel> parseLogLevel(std::string_view level)
{
    std::string lower(level);
    std::ranges::transform(lower, lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    if (lower == "trace") return LogLevel::Trace;
    if (lower == "info")  return LogLevel::Info;
    if (lower == "warn")  return LogLevel::Warn;
    if (lower == "error") return LogLevel::Error;
    if (lower == "fatal") return LogLevel::Fatal;
    return std::nullopt;
}

uint32_t resolveWorkerCount(uint32_t requested)
{
    if (requested != 0)
        return requested;

    const uint32_t hw = std::thread::hardware_concurrency();
    return hw > 1 ? hw - 1 : 1;
}

} // namespace

Application::Application(ApplicationSpec spec)
    : m_spec(std::move(spec))
{
    DEMON_ASSERT(!s_instance, "An Application instance already exists!");
    s_instance = this;

    Logger::init();
    DEMON_LOG_INFO("DemonEngine v1.2 \u2014 '{}'", m_spec.name);

    m_runtimeSettings.load("engine.config.json");
    bool hotReload = true;
    if (m_runtimeSettings.tryGetBool("runtime.hot_reload", hotReload))
        m_runtimeSettings.setHotReload(hotReload);
    applyRuntimeSettings(true);

    m_platformConfig.loadFrom(m_runtimeSettings);
    if (m_platformConfig.enableDpiAwareness())
        DemonEnableDpiAwareness();

    m_threading.setMainThread();
    ThreadingManager::setCurrentThreadName(L"DemonMain");
    m_threading.init(resolveWorkerCount(m_workerCount), m_workerAffinityMask);

    // ── Window ────────────────────────────────────────────────────────────────
    WindowSpec ws;
    ws.title      = m_spec.name;
    ws.width      = m_spec.width;
    ws.height     = m_spec.height;
    ws.fullscreen = m_spec.fullscreen;
    ws.vsync      = m_spec.vsync;
    ws.resizable  = m_spec.resizable;
#ifdef DEMON_PLATFORM_WINDOWS
    ws.nativeHandle = m_spec.nativeWindow;
#endif
    m_platformConfig.applyTo(ws);
    m_window = std::make_unique<Window>(ws);
    Input::init(m_window->getWin32Handle());

    // ── Window icon (assets/icons/demon.ico) ──────────────────────────────────
    {
        HWND hwnd = m_window->getWin32Handle();
        HICON hIcon = static_cast<HICON>(
            LoadImageA(nullptr, "assets/icon/demon.ico",
                       IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE | LR_SHARED));
        if (!hIcon) {
            hIcon = static_cast<HICON>(
                LoadImageA(nullptr, "assets/icons/demon.ico",
                           IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE | LR_SHARED));
        }
        if (hIcon) {
            SendMessage(hwnd, WM_SETICON, ICON_BIG,   reinterpret_cast<LPARAM>(hIcon));
            SendMessage(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(hIcon));
        } else {
            DEMON_LOG_WARN("Application: could not load icon from 'assets/icon' or 'assets/icons' (GetLastError={}).",
                           static_cast<uint32_t>(GetLastError()));
        }
    }
    m_dpiScale = DemonGetDpiScale(m_window->getWin32Handle());

    m_window->setEventCallback([this](Event& e) {
        EventDispatcher d(e);
        d.dispatch<WindowResizeEvent>([this](auto& ev){ onWindowResize(ev); return true; });
        for (auto it = m_layerStack.rbegin(); it != m_layerStack.rend(); ++it) {
            if (e.handled) break;
            (*it)->onEvent(e);
        }
        if (!e.handled) {
            d.dispatch<WindowCloseEvent> ([this](auto& ev){ onWindowClose(ev);  return true; });
        }
    });

    // ── Renderer ──────────────────────────────────────────────────────────────
    m_renderer = std::make_unique<Renderer>(*m_window);
    m_renderer->init();

    applyRuntimeSettings(false);

    DEMON_LOG_INFO("Application initialised successfully.");
}

Application::~Application()
{
    m_layerStack.clear();
    m_renderer->shutdown();
    m_window.reset();
    m_threading.shutdown();
    DEMON_LOG_INFO("Application shutdown complete.");
    s_instance = nullptr;
}

void Application::run()
{
    DEMON_LOG_INFO("Application run loop starting.");
    m_timer.reset();
    while (m_running) {
        if (m_platformEventPump)
            m_platformEventPump();
        if (m_runtimeSettings.update())
            applyRuntimeSettings(false);
        m_threading.updateDebug(m_threadStallWarnMs);

        m_deltaTime = m_timer.elapsedSeconds();
        m_timer.reset();
        ++m_frameCount;

        m_window->pollEvents();
        Input::update();
        if (m_minimised) continue;

        if (m_deferredResizeWidth != 0 && m_deferredResizeHeight != 0 && m_frameCount >= 3) {
            m_renderer->onResize(m_deferredResizeWidth, m_deferredResizeHeight);
            m_deferredResizeWidth = 0;
            m_deferredResizeHeight = 0;
        }

        for (auto& layer : m_layerStack)
            layer->onUpdate(m_deltaTime);

        // Build render commands for this frame (scene submission).
        for (auto& layer : m_layerStack)
            layer->onRender(*m_renderer);

        if (m_renderer->beginFrame()) {
            const float displayW = static_cast<float>(m_window->getWidth());
            const float displayH = static_cast<float>(m_window->getHeight());
            for (auto& layer : m_layerStack)
                layer->onGuiRender(*m_renderer, displayW, displayH);
            m_renderer->endFrame();
        }
    }
    DEMON_LOG_INFO("Application run loop exited. windowShouldClose={}, running={}",
                   m_window ? m_window->shouldClose() : false, m_running);
}

void Application::close() { m_running = false; }

void Application::pushLayer(std::unique_ptr<Layer> layer)
{
    m_layerStack.pushLayer(std::move(layer));
}

void Application::pushOverlay(std::unique_ptr<Layer> overlay)
{
    m_layerStack.pushOverlay(std::move(overlay));
}

void Application::onWindowClose(const WindowCloseEvent&)
{
    DEMON_LOG_INFO("Window close event received.");
    m_running = false;
}
void Application::onWindowResize(const WindowResizeEvent& e)
{
    DEMON_LOG_INFO("Application: window resize {}x{}", e.width, e.height);
    if (e.width == 0 || e.height == 0) { m_minimised = true;  return; }
    m_minimised = false;
    if (m_frameCount < 3) {
        DEMON_LOG_INFO("Application: deferring resize {}x{} until startup settles (frame={})",
                       e.width, e.height, m_frameCount);
        m_deferredResizeWidth = e.width;
        m_deferredResizeHeight = e.height;
        return;
    }
    DEMON_LOG_INFO("Application: forwarding resize {}x{} to renderer at frame {}", e.width, e.height, m_frameCount);
    m_renderer->onResize(e.width, e.height);
}

void Application::applyRuntimeSettings(bool initial)
{
    bool boolVal = false;
    int64_t intVal = 0;
    uint64_t u64Val = 0;
    std::string strVal;

    if (m_runtimeSettings.tryGetBool("window.vsync", boolVal)) {
        m_spec.vsync = boolVal;
        if (!initial && m_window)
            m_window->setVSync(boolVal);
    }

    if (initial && m_runtimeSettings.tryGetBool("window.fullscreen", boolVal))
        m_spec.fullscreen = boolVal;

    if (initial && m_runtimeSettings.tryGetBool("window.resizable", boolVal))
        m_spec.resizable = boolVal;

    if (initial && m_runtimeSettings.tryGetInt("window.width", intVal) && intVal > 0)
        m_spec.width = static_cast<uint32_t>(intVal);

    if (initial && m_runtimeSettings.tryGetInt("window.height", intVal) && intVal > 0)
        m_spec.height = static_cast<uint32_t>(intVal);

    if (initial && m_runtimeSettings.tryGetString("window.title", strVal))
        m_spec.name = strVal;

    if (m_runtimeSettings.tryGetString("logging.level", strVal)) {
        if (auto level = parseLogLevel(strVal))
            Logger::setLevel(*level);
    }

    if (initial && m_runtimeSettings.tryGetInt("threading.worker_count", intVal) && intVal >= 0)
        m_workerCount = static_cast<uint32_t>(intVal);

    if (initial && m_runtimeSettings.tryGetUInt64("threading.affinity_mask", u64Val))
        m_workerAffinityMask = u64Val;

    if (m_runtimeSettings.tryGetUInt64("threading.stall_warning_ms", u64Val))
        m_threadStallWarnMs = static_cast<uint32_t>(u64Val);

    if (m_runtimeSettings.tryGetBool("runtime.hot_reload", boolVal))
        m_runtimeSettings.setHotReload(boolVal);
}

} // namespace Demon
