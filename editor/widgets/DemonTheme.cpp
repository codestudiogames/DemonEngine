// ==============================================================================
//  DemonEngine Editor::DemonTheme  –  Implementation
//  Dark, slightly muted UI with crimson accent colours.
// ==============================================================================
#include <cmath>
#include <fstream>
#include <imgui.h>
#include "DemonTheme.h"
#include "core/Logger.h"

namespace Demon {

void DemonTheme::apply() {
    ImGuiStyle& s = ImGui::GetStyle();

    // ── Rounding & Spacing ───────────────────────────────────────────────────
    // Flat, tight metrics in the style of Unreal Engine 5's Slate theme.
    s.WindowRounding = 4.0f;
    s.ChildRounding = 4.0f;
    s.FrameRounding = 3.0f;
    s.GrabRounding = 3.0f;
    s.PopupRounding = 4.0f;
    s.ScrollbarRounding = 9.0f;
    s.TabRounding = 4.0f;
    s.WindowBorderSize = 1.0f;
    s.ChildBorderSize = 1.0f;
    s.PopupBorderSize = 1.0f;
    s.FrameBorderSize = 0.0f;
    s.TabBarBorderSize = 1.0f;
    s.TabBarOverlineSize = 2.0f;
    s.DockingSeparatorSize = 2.0f;
    s.WindowPadding = {8.0f, 8.0f};
    s.FramePadding = {8.0f, 5.0f};
    s.ItemSpacing = {8.0f, 6.0f};
    s.ItemInnerSpacing = {6.0f, 5.0f};
    s.CellPadding = {6.0f, 4.0f};
    s.IndentSpacing = 16.0f;
    s.ScrollbarSize = 12.0f;
    s.GrabMinSize = 10.0f;
    s.WindowTitleAlign = {0.0f, 0.5f};
    s.WindowMenuButtonPosition = ImGuiDir_Right;
    s.SeparatorTextBorderSize = 1.0f;
    s.SeparatorTextAlign = {0.0f, 0.5f};
    s.SeparatorTextPadding = {12.0f, 4.0f};

    // ── Colours ───────────────────────────────────────────────────────────────
    auto* c = s.Colors;
    using V4 = ImVec4;
    const V4 accent = V4(0.86f, 0.29f, 0.27f, 1.00f);   // crimson
    const V4 accentSoft = V4(0.66f, 0.21f, 0.19f, 0.85f);
    const V4 accentMute = V4(0.48f, 0.16f, 0.14f, 0.55f);
    const V4 bgDeep = V4(0.075f, 0.075f, 0.080f, 1.00f); // deepest — title bars, docking bg
    const V4 panel = V4(0.105f, 0.105f, 0.110f, 1.00f);  // window body
    const V4 panelRaised = V4(0.135f, 0.135f, 0.140f, 1.00f);
    const V4 panelHover = V4(0.180f, 0.180f, 0.190f, 1.00f);
    const V4 panelActive = V4(0.230f, 0.230f, 0.240f, 1.00f);

    // Background family
    c[ImGuiCol_WindowBg] = panel;
    c[ImGuiCol_ChildBg] = V4(0.120f, 0.120f, 0.125f, 1.00f);
    c[ImGuiCol_PopupBg] = V4(0.095f, 0.095f, 0.100f, 0.99f);
    c[ImGuiCol_MenuBarBg] = bgDeep;

    // Borders & separators
    c[ImGuiCol_Border] = V4(0.00f, 0.00f, 0.00f, 0.55f);
    c[ImGuiCol_BorderShadow] = V4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_Separator] = V4(0.045f, 0.045f, 0.050f, 1.00f);
    c[ImGuiCol_SeparatorHovered] = accentSoft;
    c[ImGuiCol_SeparatorActive] = accent;

    // Frame (input boxes, sliders, etc.) — darker than the panel, Unreal-style.
    c[ImGuiCol_FrameBg] = V4(0.060f, 0.060f, 0.065f, 1.00f);
    c[ImGuiCol_FrameBgHovered] = V4(0.100f, 0.100f, 0.108f, 1.00f);
    c[ImGuiCol_FrameBgActive] = V4(0.130f, 0.130f, 0.140f, 1.00f);

    // Title bars
    c[ImGuiCol_TitleBg] = bgDeep;
    c[ImGuiCol_TitleBgActive] = bgDeep;
    c[ImGuiCol_TitleBgCollapsed] = V4(bgDeep.x, bgDeep.y, bgDeep.z, 0.85f);

    // Scrollbar — slim, floating grab
    c[ImGuiCol_ScrollbarBg] = V4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_ScrollbarGrab] = V4(0.28f, 0.28f, 0.30f, 0.85f);
    c[ImGuiCol_ScrollbarGrabHovered] = V4(0.38f, 0.38f, 0.40f, 1.00f);
    c[ImGuiCol_ScrollbarGrabActive] = V4(0.50f, 0.50f, 0.52f, 1.00f);

    // Buttons — neutral grey, accent reserved for selection states
    c[ImGuiCol_Button] = panelRaised;
    c[ImGuiCol_ButtonHovered] = panelHover;
    c[ImGuiCol_ButtonActive] = panelActive;

    // Headers (collapsing headers, tree nodes, selectables)
    c[ImGuiCol_Header] = V4(0.180f, 0.180f, 0.190f, 0.90f);
    c[ImGuiCol_HeaderHovered] = V4(0.230f, 0.230f, 0.240f, 1.00f);
    c[ImGuiCol_HeaderActive] = accentMute;

    // Sliders / grabs
    c[ImGuiCol_SliderGrab] = accentSoft;
    c[ImGuiCol_SliderGrabActive] = accent;

    // Checkmarks & radio
    c[ImGuiCol_CheckMark] = accent;

    // Resize grips
    c[ImGuiCol_ResizeGrip] = V4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_ResizeGripHovered] = accentSoft;
    c[ImGuiCol_ResizeGripActive] = accent;

    // Tabs — flat with a crimson overline on the selected tab (UE5-style)
    c[ImGuiCol_Tab] = bgDeep;
    c[ImGuiCol_TabHovered] = panelHover;
    c[ImGuiCol_TabSelected] = panel;
    c[ImGuiCol_TabSelectedOverline] = accent;
    c[ImGuiCol_TabDimmed] = bgDeep;
    c[ImGuiCol_TabDimmedSelected] = V4(0.090f, 0.090f, 0.095f, 1.00f);
    c[ImGuiCol_TabDimmedSelectedOverline] = V4(accent.x, accent.y, accent.z, 0.30f);

    // Docking
    c[ImGuiCol_DockingPreview] = V4(accent.x, accent.y, accent.z, 0.35f);
    c[ImGuiCol_DockingEmptyBg] = bgDeep;

    // Tables
    c[ImGuiCol_TableHeaderBg] = panelRaised;
    c[ImGuiCol_TableBorderStrong] = V4(0.00f, 0.00f, 0.00f, 0.55f);
    c[ImGuiCol_TableBorderLight] = V4(0.00f, 0.00f, 0.00f, 0.30f);
    c[ImGuiCol_TableRowBg] = V4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_TableRowBgAlt] = V4(1.00f, 1.00f, 1.00f, 0.02f);

    // Text
    c[ImGuiCol_Text] = V4(0.92f, 0.92f, 0.92f, 1.00f);
    c[ImGuiCol_TextDisabled] = V4(0.50f, 0.51f, 0.53f, 1.00f);
    c[ImGuiCol_TextSelectedBg] = V4(accent.x, accent.y, accent.z, 0.30f);
    c[ImGuiCol_TextLink] = accent;

    // Drag drop
    c[ImGuiCol_DragDropTarget] = V4(accent.x, accent.y, accent.z, 0.90f);

    // Misc
    c[ImGuiCol_NavHighlight] = accent;
    c[ImGuiCol_ModalWindowDimBg] = V4(0.00f, 0.00f, 0.00f, 0.55f);
    c[ImGuiCol_PlotLines] = V4(0.66f, 0.68f, 0.72f, 1.00f);
    c[ImGuiCol_PlotLinesHovered] = V4(0.80f, 0.82f, 0.86f, 1.00f);
    c[ImGuiCol_PlotHistogram] = accentSoft;
    c[ImGuiCol_PlotHistogramHovered] = accent;

    DEMON_LOG_INFO("DemonEngine modern editor theme applied.");
}

void DemonTheme::applyFont(float size) {
    ImGuiIO& io = ImGui::GetIO();
    // Try to load a clean font; fallback to built-in Proggy
    ImFontConfig cfg;
    cfg.OversampleH = 3;
    cfg.OversampleV = 2;
    cfg.PixelSnapH  = false;

    // Preference order: bundled Inter, Windows Segoe UI variants, built-in.
    const char* fontPaths[] = {
        "assets/fonts/Inter-Regular.ttf",
        "C:/Windows/Fonts/segoeuivar.ttf",
        "C:/Windows/Fonts/segoeui.ttf",
    };
    ImFont* font = nullptr;
    for (const char* fontPath : fontPaths) {
        std::ifstream check(fontPath);
        if (!check.good())
            continue;
        font = io.Fonts->AddFontFromFileTTF(fontPath, size, &cfg);
        if (font)
            break;
    }
    if (!font)
        font = io.Fonts->AddFontDefault();
    io.FontDefault = font;

    // ── Monospace face for the Debug View overlay ─────────────────────────────
    // The debug HUD is column aligned, so it needs a fixed-pitch face. ImGui
    // 1.92 sizes fonts dynamically, so one face covers every overlay size.
    ImFontConfig monoCfg;
    monoCfg.OversampleH = 2;
    monoCfg.OversampleV = 1;
    monoCfg.PixelSnapH  = true;

    const char* monoPaths[] = {
        "assets/fonts/JetBrainsMono-Regular.ttf",
        "assets/fonts/RobotoMono-Regular.ttf",
        "assets/fonts/DejaVuSansMono.ttf",
        "C:/Windows/Fonts/consola.ttf",
        "C:/Windows/Fonts/lucon.ttf",
        "C:/Windows/Fonts/cour.ttf",
    };

    s_monoFont = nullptr;
    s_monoBaseSize = size;
    for (const char* candidate : monoPaths) {
        std::ifstream check(candidate);
        if (!check.good())
            continue;
        s_monoFont = io.Fonts->AddFontFromFileTTF(candidate, size, &monoCfg);
        if (s_monoFont) {
            DEMON_LOG_INFO("Debug view monospace font loaded from '{}'.", candidate);
            break;
        }
    }
    if (!s_monoFont)
        DEMON_LOG_WARN("No monospace font found for the debug view; falling back to the UI font.");

    DEMON_LOG_INFO("Font loaded (size {}px).", size);
}

ImFont* DemonTheme::s_monoFont     = nullptr;
float   DemonTheme::s_monoBaseSize = 15.0f;

} // namespace Demon
