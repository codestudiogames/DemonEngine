#pragma once
// =============================================================================
//  DemonGUI::GUIDockSpace  —  Tabbed fixed-layout panel manager
//  Layout: [Left | Centre | Right] stacked, Bottom bar below all three.
//  Each slot hosts a tab strip. Panels register into a slot.
// =============================================================================
#include "GUITypes.h"
#include "GUIContext.h"

namespace Demon::GUI {

// Dock slots
enum class DockSlot { Left = 0, Centre, Right, Bottom, COUNT };

struct TabState {
    std::string panelId;
    std::string title;
    bool        visible = true;
};

struct SlotState {
    DockSlot             slot;
    Rect                 rect{};
    std::vector<TabState> tabs;
    int                  activeTab = 0;
};

class GUIDockSpace {
public:
    GUIDockSpace() = default;

    // Call once at startup
    void init(float displayW, float displayH);

    // Register a panel into a slot
    void addPanel(const std::string& id, const std::string& title, DockSlot slot);

    // Call each frame before panels render.
    // Returns the content rect for the currently active panel in each slot.
    void beginFrame(GUIContext& ctx, float displayW, float displayH);

    // Draw tab bars and return content rect for a given slot's active panel
    [[nodiscard]] Rect  getContentRect(DockSlot slot) const;
    [[nodiscard]] bool  isPanelActive (DockSlot slot, const std::string& panelId) const;
    [[nodiscard]] const std::string& activePanelId(DockSlot slot) const;

    // Layout persistence
    void saveLayout (const std::string& path) const;
    bool loadLayout (const std::string& path);
    void resetLayout(float displayW, float displayH);

private:
    void recalcRects(float w, float h);
    void drawTabBar (GUIContext& ctx, SlotState& slot);

    static constexpr float k_tabH    = 26.f;
    static constexpr float k_leftW   = 0.18f;   // fraction of display width
    static constexpr float k_rightW  = 0.20f;
    static constexpr float k_bottomH = 0.20f;   // fraction of display height
    static constexpr float k_menuH   = 24.f;    // top menu bar

    std::array<SlotState, size_t(DockSlot::COUNT)> m_slots;
    float m_displayW = 1280.f;
    float m_displayH = 720.f;
};

} // namespace Demon::GUI
