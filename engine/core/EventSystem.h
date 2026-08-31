#pragma once
// ==============================================================================
//  DemonEngine::EventSystem
//  Lightweight, synchronous, type-safe event dispatching.
// ==============================================================================
#include <functional>
#include <string>
#include <typeindex>
#include <cstdint>

namespace Demon {

// ── Event base ────────────────────────────────────────────────────────────────
struct Event {
    bool handled = false;
    virtual ~Event() = default;
    virtual const char* name() const = 0;
};

// ── Dispatcher ────────────────────────────────────────────────────────────────
class EventDispatcher {
public:
    explicit EventDispatcher(Event& event) : m_event(event) {}

    template<typename T, typename Fn>
    bool dispatch(Fn&& fn) {
        if (!m_event.handled && dynamic_cast<T*>(&m_event)) {
            m_event.handled = fn(static_cast<T&>(m_event));
            return true;
        }
        return false;
    }

private:
    Event& m_event;
};

// ── Window Events ─────────────────────────────────────────────────────────────
struct WindowCloseEvent  : Event { const char* name() const override { return "WindowClose"; } };

struct WindowResizeEvent : Event {
    uint32_t width, height;
    WindowResizeEvent(uint32_t w, uint32_t h) : width(w), height(h) {}
    const char* name() const override { return "WindowResize"; }
};

// ── Keyboard Events ───────────────────────────────────────────────────────────
struct KeyPressedEvent : Event {
    int key, mods, repeatCount;
    KeyPressedEvent(int k, int m, int r) : key(k), mods(m), repeatCount(r) {}
    const char* name() const override { return "KeyPressed"; }
};

struct KeyReleasedEvent : Event {
    int key, mods;
    KeyReleasedEvent(int k, int m) : key(k), mods(m) {}
    const char* name() const override { return "KeyReleased"; }
};

struct KeyTypedEvent : Event {
    unsigned int codepoint;
    explicit KeyTypedEvent(unsigned int c) : codepoint(c) {}
    const char* name() const override { return "KeyTyped"; }
};

// ── Mouse Events ──────────────────────────────────────────────────────────────
struct MouseButtonPressedEvent : Event {
    int button, mods;
    MouseButtonPressedEvent(int b, int m) : button(b), mods(m) {}
    const char* name() const override { return "MouseButtonPressed"; }
};

struct MouseButtonReleasedEvent : Event {
    int button, mods;
    MouseButtonReleasedEvent(int b, int m) : button(b), mods(m) {}
    const char* name() const override { return "MouseButtonReleased"; }
};

struct MouseMovedEvent : Event {
    float x, y;
    MouseMovedEvent(float x_, float y_) : x(x_), y(y_) {}
    const char* name() const override { return "MouseMoved"; }
};

struct MouseScrolledEvent : Event {
    float xOffset, yOffset;
    MouseScrolledEvent(float x, float y) : xOffset(x), yOffset(y) {}
    const char* name() const override { return "MouseScrolled"; }
};

// ── App Events ────────────────────────────────────────────────────────────────
struct AppTickEvent   : Event { const char* name() const override { return "AppTick"; } };
struct AppUpdateEvent : Event { const char* name() const override { return "AppUpdate"; } };
struct AppRenderEvent : Event { const char* name() const override { return "AppRender"; } };

} // namespace Demon
