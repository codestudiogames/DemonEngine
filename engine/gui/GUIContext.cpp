// =============================================================================
//  DemonGUI::GUIContext  —  Widget implementation
// =============================================================================
#include "GUIContext.h"
#include <cstring>
#include <cstdio>

namespace Demon::GUI {

// ── FNV-1a hash for string IDs ────────────────────────────────────────────────
static WidgetID fnv1a(const char* s, uint32_t seed = 2166136261u)
{
    for (; *s; ++s) seed = (seed ^ uint8_t(*s)) * 16777619u;
    return seed ? seed : 1u;
}

WidgetID GUIContext::makeID(const char* str)         { return fnv1a(str); }
WidgetID GUIContext::makeID(const char* str, int idx) {
    char buf[128]; snprintf(buf, sizeof(buf), "%s_%d", str, idx); return fnv1a(buf);
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────
void GUIContext::init(float w, float h) { m_displayW = w; m_displayH = h; }

void GUIContext::beginFrame(const GUIInput& input, float w, float h)
{
    m_input    = input;
    m_displayW = w;
    m_displayH = h;
    m_dl.reset();
    m_tooltip.clear();
    m_hot = ID_NONE;
}

void GUIContext::endFrame()
{
    // Draw tooltip on top
    if (!m_tooltip.empty()) {
        Vec2 sz = activeFont().measureText(m_tooltip);
        Rect tr{ m_input.mousePos.x + 12.f, m_input.mousePos.y + 12.f,
                 sz.x + 12.f, sz.y + 8.f };
        m_dl.addRectFilled(tr, {0.1f,0.1f,0.1f,0.95f});
        m_dl.addRect(tr, Palette::Separator);
        activeFont().drawText(m_dl, {tr.x+6.f, tr.y+4.f}, m_tooltip, Palette::Text);
    }
    // Release active if mouse released
    if (!m_input.mouseDown[0])
        m_active = m_dragId = ID_NONE;
}

// ── Input helpers ─────────────────────────────────────────────────────────────
bool GUIContext::isHovered(Rect r) const { return r.contains(m_input.mousePos); }
bool GUIContext::isClicked(Rect r, int btn) const {
    return m_input.mouseClick[btn] && r.contains(m_input.mousePos);
}

// ── Low-level draw ────────────────────────────────────────────────────────────
void GUIContext::drawText(Vec2 pos, const std::string& t, Color c, float wrap)
{ activeFont().drawText(m_dl, pos, t, c, wrap); }

void GUIContext::drawTextCentered(Rect r, const std::string& t, Color c) {
    Vec2 sz = activeFont().measureText(t);
    activeFont().drawText(m_dl,
        {r.x + (r.w - sz.x)*0.5f, r.y + (r.h - sz.y)*0.5f}, t, c);
}

void GUIContext::drawRect(Rect r, Color c)                  { m_dl.addRect(r, c); }
void GUIContext::drawRectFilled(Rect r, Color c, float rnd) { m_dl.addRectFilled(r, c, rnd); }
void GUIContext::drawRectGrad(Rect r, Color t, Color b)     { m_dl.addRectGrad(r, t, b); }
void GUIContext::drawLine(Vec2 a, Vec2 b, Color c, float th){ m_dl.addLine(a, b, c, th); }
void GUIContext::drawImage(GUITextureID tex, Rect r, Color tint){ m_dl.addImage(tex, r, tint); }
void GUIContext::drawTriangle(Vec2 a, Vec2 b, Vec2 c, Color col){ m_dl.addTriangleFilled(a,b,c,col); }
void GUIContext::separator(Rect r) { m_dl.addRectFilled({r.x, r.y+r.h*0.5f-0.5f, r.w, 1.f}, Palette::Separator); }
void GUIContext::setTooltip(const std::string& t) { if (m_tooltip.empty()) m_tooltip = t; }

// ── Button ────────────────────────────────────────────────────────────────────
bool GUIContext::button(WidgetID id, Rect r, const std::string& label, Color bg)
{
    bool hov = isHovered(r);
    bool clk = isClicked(r);
    if (hov) m_hot = id;
    Color col = hov ? Color{bg.r*1.15f, bg.g*1.15f, bg.b*1.15f, bg.a} : bg;
    if (m_active == id) col = Palette::Accent;
    m_dl.addRectFilled(r, col);
    m_dl.addRect(r, hov ? Palette::Accent : Palette::Separator);
    drawTextCentered(r, label, Palette::TextBright);
    if (clk) m_active = id;
    return m_input.mouseRelease[0] && m_active == id && hov;
}

// ── Checkbox ──────────────────────────────────────────────────────────────────
bool GUIContext::checkbox(WidgetID id, Rect r, const std::string& label, bool& value)
{
    float sz  = r.h - 4.f;
    Rect  box = {r.x + 2.f, r.y + 2.f, sz, sz};
    bool  hov = isHovered(r);
    bool  clk = isClicked(r);
    if (hov) m_hot = id;
    m_dl.addRectFilled(box, Palette::InputBg);
    m_dl.addRect(box, hov ? Palette::Accent : Palette::Separator);
    if (value) {
        Rect inner{box.x+3, box.y+3, box.w-6, box.h-6};
        m_dl.addRectFilled(inner, Palette::CheckOn);
    }
    activeFont().drawText(m_dl, {r.x + sz + 8.f, r.y + (r.h - activeFont().lineHeight())*0.5f},
                          label, Palette::Text);
    if (clk) { value = !value; return true; }
    return false;
}

// ── SliderFloat ───────────────────────────────────────────────────────────────
bool GUIContext::sliderFloat(WidgetID id, Rect r, const std::string& label,
                              float& value, float vmin, float vmax)
{
    // label on left, track on right half
    float lw = r.w * 0.40f;
    Rect  lr = {r.x, r.y, lw, r.h};
    Rect  tr = {r.x + lw + 4.f, r.y + 2.f, r.w - lw - 4.f, r.h - 4.f};

    activeFont().drawText(m_dl, {lr.x, lr.y + (lr.h - activeFont().lineHeight())*0.5f},
                          label, Palette::Text);

    bool hov = isHovered(tr);
    if (hov) m_hot = id;

    m_dl.addRectFilled(tr, Palette::InputBg);
    m_dl.addRect(tr, hov ? Palette::Accent : Palette::Separator);

    float t = (value - vmin) / (vmax - vmin);
    t = std::clamp(t, 0.f, 1.f);
    Rect fill{tr.x, tr.y, tr.w * t, tr.h};
    m_dl.addRectFilled(fill, Palette::SliderFill);

    // Value label
    char buf[32]; snprintf(buf, sizeof(buf), "%.3f", value);
    drawTextCentered(tr, buf, Palette::TextBright);

    bool changed = false;
    if (m_input.mouseDown[0] && hov && m_dragId == ID_NONE) {
        m_dragId = id; m_active = id;
    }
    if (m_dragId == id && m_input.mouseDown[0]) {
        float newT = std::clamp((m_input.mousePos.x - tr.x) / tr.w, 0.f, 1.f);
        float newVal = vmin + newT * (vmax - vmin);
        if (newVal != value) { value = newVal; changed = true; }
    }
    return changed;
}

// ── SliderInt ─────────────────────────────────────────────────────────────────
bool GUIContext::sliderInt(WidgetID id, Rect r, const std::string& label,
                           int& value, int vmin, int vmax)
{
    float fv = static_cast<float>(value);
    bool changed = sliderFloat(id, r, label, fv,
                               static_cast<float>(vmin), static_cast<float>(vmax));
    if (changed) value = static_cast<int>(fv + 0.5f);
    return changed;
}

// ── ColorEdit3 ────────────────────────────────────────────────────────────────
bool GUIContext::colorEdit3(WidgetID id, Rect r, const std::string& label, float rgb[3])
{
    float lw = r.w * 0.40f;
    activeFont().drawText(m_dl, {r.x, r.y + (r.h - activeFont().lineHeight())*0.5f},
                          label, Palette::Text);

    float sw = (r.w - lw - 8.f) / 3.f;
    bool changed = false;
    const char* ch[] = {"R","G","B"};
    for (int i = 0; i < 3; ++i) {
        Rect sr{r.x + lw + 4.f + sw*i + i*2.f, r.y + 2.f, sw - 2.f, r.h - 4.f};
        WidgetID sid = makeID(label.c_str(), i + id*10);
        changed |= sliderFloat(sid, sr, ch[i], rgb[i], 0.f, 1.f);
    }
    // Colour preview swatch
    Rect swatch{r.x + r.w - 20.f, r.y + 2.f, 18.f, r.h - 4.f};
    m_dl.addRectFilled(swatch, {rgb[0], rgb[1], rgb[2], 1.f});
    m_dl.addRect(swatch, Palette::Separator);
    return changed;
}

// ── InputText ─────────────────────────────────────────────────────────────────
bool GUIContext::inputText(WidgetID id, Rect r, std::string& text,
                           const std::string& hint)
{
    bool hov = isHovered(r);
    bool clk = isClicked(r);
    if (hov) m_hot = id;
    if (clk) { m_focused = id; m_textInput.owner = id; m_textInput.buffer = text;
               m_textInput.cursor = static_cast<int>(text.size()); }

    bool focused = (m_focused == id);
    m_dl.addRectFilled(r, focused ? Palette::InputActive : Palette::InputBg);
    m_dl.addRect(r, focused ? Palette::Accent : Palette::Separator);

    bool changed = false;
    if (focused) {
        // Handle keyboard input
        if (m_input.keyChar >= 32 && m_input.keyChar < 127) {
            text.insert(m_textInput.cursor, 1, char(m_input.keyChar));
            m_textInput.cursor++;
            changed = true;
        }
        if (m_input.keyBackspace && !text.empty() && m_textInput.cursor > 0) {
            text.erase(m_textInput.cursor - 1, 1);
            m_textInput.cursor--;
            changed = true;
        }
        if (m_input.keyLeft  && m_textInput.cursor > 0)                     m_textInput.cursor--;
        if (m_input.keyRight && m_textInput.cursor < (int)text.size())      m_textInput.cursor++;
        if (m_input.keyEnter || m_input.keyEscape) m_focused = ID_NONE;
    }

    const std::string& display = text.empty() && !focused ? hint : text;
    Color textCol = text.empty() && !focused ? Palette::TextDisabled : Palette::TextBright;
    activeFont().drawText(m_dl, {r.x + 4.f, r.y + (r.h - activeFont().lineHeight())*0.5f},
                          display, textCol);

    // Cursor
    if (focused) {
        std::string before = text.substr(0, m_textInput.cursor);
        float cx = r.x + 4.f + activeFont().measureText(before).x;
        m_dl.addRectFilled({cx, r.y+3, 1.5f, r.h-6}, Palette::TextBright);
    }
    return changed;
}

// ── InputFloat ───────────────────────────────────────────────────────────────
bool GUIContext::inputFloat(WidgetID id, Rect r, const std::string& label,
                            float& value, const char* fmt)
{
    float lw = r.w * 0.45f;
    activeFont().drawText(m_dl, {r.x, r.y + (r.h - activeFont().lineHeight())*0.5f},
                          label, Palette::Text);
    Rect ir{r.x + lw, r.y, r.w - lw, r.h};
    std::string str;
    char buf[64]; snprintf(buf, sizeof(buf), fmt, value); str = buf;
    bool changed = inputText(id, ir, str);
    if (changed) {
        try { value = std::stof(str); } catch (...) {}
    }
    return changed;
}

// ── InputFloat3 ──────────────────────────────────────────────────────────────
bool GUIContext::inputFloat3(WidgetID id, Rect r, const std::string& label, float v[3])
{
    float lw = r.w * 0.35f;
    activeFont().drawText(m_dl, {r.x, r.y + (r.h - activeFont().lineHeight())*0.5f},
                          label, Palette::Text);
    float fw = (r.w - lw - 8.f) / 3.f;
    bool changed = false;
    for (int i = 0; i < 3; ++i) {
        Rect ir{r.x + lw + 4.f + fw*i + i*2.f, r.y, fw - 2.f, r.h};
        changed |= inputFloat(makeID(label.c_str(), i + id), ir, "", v[i]);
    }
    return changed;
}

// ── TreeNode ──────────────────────────────────────────────────────────────────
bool GUIContext::treeNode(WidgetID id, Rect r, const std::string& label,
                          bool& open, bool selected, int depth)
{
    float indent = depth * 14.f + 4.f;
    bool hov = isHovered(r);
    bool clk = isClicked(r);
    if (hov) m_hot = id;

    Color bg = selected  ? Color{0.25f, 0.25f, 0.40f, 1.f} :
               hov       ? Palette::PanelHover : Palette::Panel;
    m_dl.addRectFilled(r, bg);

    // Arrow
    float ax = r.x + indent;
    float ay = r.y + r.h * 0.5f;
    if (open)
        m_dl.addTriangleFilled({ax,ay-4},{ax+8,ay-4},{ax+4,ay+4}, Palette::TextDisabled);
    else
        m_dl.addTriangleFilled({ax,ay-5},{ax,ay+5},{ax+6,ay}, Palette::TextDisabled);

    activeFont().drawText(m_dl, {ax + 12.f, r.y + (r.h - activeFont().lineHeight())*0.5f},
                          label, selected ? Palette::TextBright : Palette::Text);

    if (clk) { open = !open; return true; }
    return false;
}

// ── ScrollRegion ──────────────────────────────────────────────────────────────
void GUIContext::beginScrollRegion(WidgetID id, Rect r, float contentH, float& scrollY)
{
    float& off = m_scrollOffsets[id];
    if (isHovered(r)) off -= m_input.scrollY * 20.f;
    off = std::clamp(off, 0.f, std::max(0.f, contentH - r.h));
    scrollY = off;
    m_scrollClipRestore = m_dl.currentClip();
    m_dl.pushClip(r);
    m_scrollRegionActive = id;
}

void GUIContext::endScrollRegion(WidgetID /*id*/)
{
    m_dl.popClip();
    m_scrollRegionActive = ID_NONE;
}

} // namespace Demon::GUI
