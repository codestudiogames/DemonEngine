// =============================================================================
//  DemonGUI::GUIDockSpace  —  Implementation
// =============================================================================
#include "GUIDockSpace.h"
#include "core/Logger.h"
#include <fstream>
#include <sstream>

namespace Demon::GUI {

void GUIDockSpace::init(float w, float h)
{
    for (int i = 0; i < int(DockSlot::COUNT); ++i)
        m_slots[i].slot = DockSlot(i);
    resetLayout(w, h);
}

void GUIDockSpace::addPanel(const std::string& id, const std::string& title, DockSlot slot)
{
    auto& s = m_slots[size_t(slot)];
    for (auto& t : s.tabs) if (t.panelId == id) return; // no duplicates
    s.tabs.push_back({id, title, true});
}

void GUIDockSpace::recalcRects(float w, float h)
{
    m_displayW = w; m_displayH = h;
    float lw = w * k_leftW;
    float rw = w * k_rightW;
    float bh = h * k_bottomH;
    float mh = k_menuH;
    float cw = w - lw - rw;
    float ch = h - mh - bh;

    // Left panel (scene hierarchy)
    m_slots[size_t(DockSlot::Left)].rect   = {0,    mh, lw, ch};
    // Centre (viewport)
    m_slots[size_t(DockSlot::Centre)].rect = {lw,   mh, cw, ch};
    // Right (properties)
    m_slots[size_t(DockSlot::Right)].rect  = {lw+cw,mh, rw, ch};
    // Bottom (console / content browser)
    m_slots[size_t(DockSlot::Bottom)].rect = {0,  mh+ch, w, bh};
}

void GUIDockSpace::resetLayout(float w, float h) { recalcRects(w, h); }

void GUIDockSpace::beginFrame(GUIContext& ctx, float w, float h)
{
    if (w != m_displayW || h != m_displayH) recalcRects(w, h);

    // Background fill
    ctx.drawRectFilled({0, 0, w, h}, Palette::Background);

    // Draw each slot's tab bar
    for (auto& slot : m_slots) {
        if (slot.tabs.empty()) continue;
        drawTabBar(ctx, slot);
    }
}

void GUIDockSpace::drawTabBar(GUIContext& ctx, SlotState& slot)
{
    Rect bar = {slot.rect.x, slot.rect.y, slot.rect.w, k_tabH};
    ctx.drawRectFilled(bar, Palette::Header);
    ctx.drawLine({bar.x, bar.y + bar.h}, {bar.x + bar.w, bar.y + bar.h},
                 Palette::Separator, 1.f);

    float tx = bar.x + 4.f;
    for (int i = 0; i < (int)slot.tabs.size(); ++i) {
        auto& tab = slot.tabs[i];
        if (!tab.visible) continue;

        Vec2 tsz = ctx.font().measureText(tab.title);
        float tw = tsz.x + 20.f;
        Rect  tr = {tx, bar.y + 2.f, tw, bar.h - 2.f};

        bool active = (i == slot.activeTab);
        bool hov    = ctx.isHovered(tr);
        Color bg = active ? Palette::TabActive : (hov ? Palette::PanelHover : Palette::TabInactive);
        ctx.drawRectFilled(tr, bg);
        if (active)
            ctx.drawRectFilled({tr.x, tr.y, tr.w, 2.f}, Palette::Accent);
        ctx.drawTextCentered(tr, tab.title,
                             active ? Palette::TextBright : Palette::TextDisabled);

        if (ctx.isClicked(tr)) slot.activeTab = i;
        tx += tw + 2.f;
    }

    // Content area background
    Rect content{slot.rect.x, slot.rect.y + k_tabH,
                 slot.rect.w, slot.rect.h - k_tabH};
    ctx.drawRectFilled(content, Palette::Panel);
    ctx.drawRect(content, Palette::Separator);
}

Rect GUIDockSpace::getContentRect(DockSlot slot) const
{
    const auto& s = m_slots[size_t(slot)];
    return {s.rect.x, s.rect.y + k_tabH, s.rect.w, s.rect.h - k_tabH};
}

bool GUIDockSpace::isPanelActive(DockSlot slot, const std::string& panelId) const
{
    const auto& s = m_slots[size_t(slot)];
    if (s.tabs.empty()) return false;
    int idx = s.activeTab;
    if (idx < 0 || idx >= (int)s.tabs.size()) return false;
    return s.tabs[idx].panelId == panelId;
}

const std::string& GUIDockSpace::activePanelId(DockSlot slot) const
{
    static const std::string empty;
    const auto& s = m_slots[size_t(slot)];
    if (s.tabs.empty() || s.activeTab >= (int)s.tabs.size()) return empty;
    return s.tabs[s.activeTab].panelId;
}

// ── Layout persistence (minimal key=value format) ─────────────────────────────
void GUIDockSpace::saveLayout(const std::string& path) const
{
    std::ofstream f(path);
    if (!f.is_open()) return;
    for (int si = 0; si < int(DockSlot::COUNT); ++si) {
        const auto& s = m_slots[si];
        f << "slot=" << si << " active=" << s.activeTab << "\n";
        for (auto& t : s.tabs)
            f << "  tab=" << t.panelId << " title=" << t.title
              << " visible=" << t.visible << "\n";
    }
    DEMON_LOG_INFO("GUIDockSpace: layout saved to '{}'.", path);
}

bool GUIDockSpace::loadLayout(const std::string& path)
{
    std::ifstream f(path);
    if (!f.is_open()) return false;

    // Reset tab active indices only (don't reset registered tabs)
    for (auto& s : m_slots) s.activeTab = 0;

    std::string line;
    int curSlot = -1;
    while (std::getline(f, line)) {
        if (line.find("slot=") == 0) {
            auto p = line.find("active=");
            if (p != std::string::npos) {
                curSlot = std::stoi(line.substr(5));
                int active = std::stoi(line.substr(p + 7));
                if (curSlot >= 0 && curSlot < int(DockSlot::COUNT))
                    m_slots[curSlot].activeTab = active;
            }
        } else if (line.find("  tab=") == 0 && curSlot >= 0) {
            // find tab by id and restore visibility
            auto vid = line.find("visible=");
            if (vid != std::string::npos) {
                std::string id = line.substr(6, line.find(' ', 6) - 6);
                bool vis = line.substr(vid + 8, 1) == "1";
                for (auto& t : m_slots[curSlot].tabs)
                    if (t.panelId == id) { t.visible = vis; break; }
            }
        }
    }
    DEMON_LOG_INFO("GUIDockSpace: layout loaded from '{}'.", path);
    return true;
}

} // namespace Demon::GUI
