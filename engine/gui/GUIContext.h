#pragma once
// =============================================================================
//  DemonGUI::GUIContext  —  Central GUI state + immediate-mode widget API
//  One instance per application.  Call beginFrame() / endFrame() each frame.
// =============================================================================
#include "GUITypes.h"
#include "GUIDrawList.h"
#include "GUIFont.h"

namespace Demon::GUI {

// ── Widget ID ────────────────────────────────────────────────────────────────
using WidgetID = uint32_t;
constexpr WidgetID ID_NONE = 0;

// ── Text input state ─────────────────────────────────────────────────────────
struct TextInputState {
    std::string buffer;
    int         cursor   = 0;
    int         selStart = -1;
    WidgetID    owner    = ID_NONE;
};

class GUIContext {
public:
    GUIContext() = default;

    // ── Lifecycle ─────────────────────────────────────────────────────────────
    void init(float displayW, float displayH);
    void beginFrame(const GUIInput& input, float displayW, float displayH);
    void endFrame();   // finalises draw list

    // ── Draw list access (GUIRenderer reads this) ─────────────────────────────
    [[nodiscard]] const GUIDrawList& drawList() const { return m_dl; }
    [[nodiscard]] GUIDrawList&       drawList()       { return m_dl; }
    [[nodiscard]] GUIFont&           font()           { return m_font; }
    void setFont(GUIFont* font) { m_externalFont = font; }

    // ── Clip ──────────────────────────────────────────────────────────────────
    void pushClip(Rect r) { m_dl.pushClip(r); }
    void popClip()        { m_dl.popClip(); }

    // ── Low-level draw helpers (panels use these) ─────────────────────────────
    void drawText (Vec2 pos, const std::string& text, Color col = Palette::Text,
                   float wrapWidth = 0.f);
    void drawTextCentered(Rect r, const std::string& text, Color col = Palette::Text);
    void drawRect (Rect r, Color col);
    void drawRectFilled(Rect r, Color col, float rounding = 0.f);
    void drawRectGrad  (Rect r, Color top, Color bot);
    void drawLine (Vec2 a, Vec2 b, Color col, float thickness = 1.f);
    void drawImage(GUITextureID tex, Rect r, Color tint = {1,1,1,1});
    void drawTriangle(Vec2 a, Vec2 b, Vec2 c, Color col);

    // ── Immediate-mode widgets ────────────────────────────────────────────────
    // Returns true on click / change.

    bool button     (WidgetID id, Rect r, const std::string& label,
                     Color bg = Palette::Panel);
    bool checkbox   (WidgetID id, Rect r, const std::string& label, bool& value);
    bool sliderFloat(WidgetID id, Rect r, const std::string& label,
                     float& value, float vmin, float vmax);
    bool sliderInt  (WidgetID id, Rect r, const std::string& label,
                     int& value, int vmin, int vmax);
    bool colorEdit3 (WidgetID id, Rect r, const std::string& label, float rgb[3]);
    bool inputText  (WidgetID id, Rect r, std::string& text,
                     const std::string& hint = "");
    bool inputFloat (WidgetID id, Rect r, const std::string& label,
                     float& value, const char* fmt = "%.3f");
    bool inputFloat3(WidgetID id, Rect r, const std::string& label, float v[3]);

    // ── Tree node (returns true if open) ─────────────────────────────────────
    bool treeNode   (WidgetID id, Rect r, const std::string& label,
                     bool& open, bool selected = false, int depth = 0);

    // ── Scrollable region ─────────────────────────────────────────────────────
    void beginScrollRegion(WidgetID id, Rect r, float contentH, float& scrollY);
    void endScrollRegion  (WidgetID id);

    // ── Separator ─────────────────────────────────────────────────────────────
    void separator(Rect r);

    // ── Tooltip ───────────────────────────────────────────────────────────────
    void setTooltip(const std::string& text);

    // ── Input query ──────────────────────────────────────────────────────────
    [[nodiscard]] bool   isHovered(Rect r) const;
    [[nodiscard]] bool   isClicked(Rect r, int btn = 0) const;
    [[nodiscard]] Vec2   mousePos() const { return m_input.mousePos; }
    [[nodiscard]] float  scrollDelta() const { return m_input.scrollY; }

    // ── ID helpers ────────────────────────────────────────────────────────────
    static WidgetID makeID(const char* str);
    static WidgetID makeID(const char* str, int idx);

private:
    GUIFont  m_font;
    GUIFont* m_externalFont = nullptr;   // if set, used instead of m_font
    GUIFont& activeFont() { return m_externalFont ? *m_externalFont : m_font; }

    GUIDrawList m_dl;
    GUIInput    m_input{};
    float       m_displayW = 1280.f;
    float       m_displayH = 720.f;

    WidgetID    m_hot     = ID_NONE;   // hovered
    WidgetID    m_active  = ID_NONE;   // pressed
    WidgetID    m_focused = ID_NONE;   // keyboard focus

    TextInputState m_textInput;

    // Scroll state per scrollable region
    struct ScrollState { float contentH = 0.f; };
    std::unordered_map<WidgetID, float> m_scrollOffsets;
    WidgetID m_scrollRegionActive = ID_NONE;
    Rect     m_scrollClipRestore  {};

    std::string m_tooltip;

    // slider drag state
    WidgetID m_dragId    = ID_NONE;
    float    m_dragStart = 0.f;
    float    m_dragValueStart = 0.f;
};

} // namespace Demon::GUI
