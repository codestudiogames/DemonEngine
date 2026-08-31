#pragma once
// =============================================================================
//  DemonGUI::GUITypes  —  Core types, handles, and math primitives
// =============================================================================
#include "core/DemonPCH.h"

namespace Demon::GUI {

// ── Scalar types ──────────────────────────────────────────────────────────────
using GUITextureID = D3D12_GPU_DESCRIPTOR_HANDLE;  // matches RHITexture::getSrvGpu()

// ── 2D math (no GLM dependency in GUI layer) ─────────────────────────────────
struct Vec2 {
    float x = 0.f, y = 0.f;
    Vec2() = default;
    Vec2(float x_, float y_) : x(x_), y(y_) {}
    Vec2 operator+(Vec2 o) const { return {x+o.x, y+o.y}; }
    Vec2 operator-(Vec2 o) const { return {x-o.x, y-o.y}; }
    Vec2 operator*(float s) const { return {x*s, y*s}; }
    bool operator==(Vec2 o) const { return x==o.x && y==o.y; }
};

struct Vec4 {
    float x = 0.f, y = 0.f, z = 0.f, w = 1.f;
    Vec4() = default;
    Vec4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
};

struct Rect {
    float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
    Rect() = default;
    Rect(float x_, float y_, float w_, float h_) : x(x_), y(y_), w(w_), h(h_) {}
    [[nodiscard]] bool contains(Vec2 p) const {
        return p.x >= x && p.x < x+w && p.y >= y && p.y < y+h;
    }
    [[nodiscard]] Vec2 min() const { return {x, y}; }
    [[nodiscard]] Vec2 max() const { return {x+w, y+h}; }
};

// ── Colour (RGBA 0-1 floats) ──────────────────────────────────────────────────
struct Color {
    float r=1,g=1,b=1,a=1;
    constexpr Color() = default;
    constexpr Color(float r_,float g_,float b_,float a_=1.f):r(r_),g(g_),b(b_),a(a_){}
    [[nodiscard]] uint32_t toRGBA8() const {
        auto u=[](float v){ return static_cast<uint32_t>(std::clamp(v,0.f,1.f)*255.f+.5f); };
        return (u(a)<<24)|(u(b)<<16)|(u(g)<<8)|u(r);
    }
    static Color fromHex(uint32_t hex, float a=1.f) {
        return { ((hex>>16)&0xFF)/255.f, ((hex>>8)&0xFF)/255.f, (hex&0xFF)/255.f, a };
    }
};

// ── DemonGUI colour palette ───────────────────────────────────────────────────
namespace Palette {
    inline constexpr Color Background   { 0.13f, 0.13f, 0.13f, 1.f };
    inline constexpr Color Panel        { 0.17f, 0.17f, 0.17f, 1.f };
    inline constexpr Color PanelHover   { 0.21f, 0.21f, 0.21f, 1.f };
    inline constexpr Color Header       { 0.10f, 0.10f, 0.10f, 1.f };
    inline constexpr Color TabActive    { 0.24f, 0.24f, 0.24f, 1.f };
    inline constexpr Color TabInactive  { 0.15f, 0.15f, 0.15f, 1.f };
    inline constexpr Color Accent       { 0.70f, 0.10f, 0.10f, 1.f }; // demon red
    inline constexpr Color AccentHover  { 0.85f, 0.15f, 0.15f, 1.f };
    inline constexpr Color Text         { 0.85f, 0.85f, 0.85f, 1.f };
    inline constexpr Color TextDisabled { 0.45f, 0.45f, 0.45f, 1.f };
    inline constexpr Color TextBright   { 1.00f, 1.00f, 1.00f, 1.f };
    inline constexpr Color Separator    { 0.28f, 0.28f, 0.28f, 1.f };
    inline constexpr Color InputBg      { 0.20f, 0.20f, 0.20f, 1.f };
    inline constexpr Color InputActive  { 0.25f, 0.25f, 0.35f, 1.f };
    inline constexpr Color SliderFill   { 0.70f, 0.10f, 0.10f, 1.f };
    inline constexpr Color CheckOn      { 0.70f, 0.10f, 0.10f, 1.f };
    inline constexpr Color LogInfo      { 0.70f, 0.85f, 1.00f, 1.f };
    inline constexpr Color LogWarn      { 1.00f, 0.80f, 0.20f, 1.f };
    inline constexpr Color LogError     { 1.00f, 0.35f, 0.35f, 1.f };
}

// ── Input state passed to GUIContext each frame ───────────────────────────────
struct GUIInput {
    Vec2  mousePos    {};
    bool  mouseDown[3]{};       // L, R, M
    bool  mouseClick[3]{};      // pressed this frame
    bool  mouseRelease[3]{};
    float scrollY     = 0.f;
    uint32_t keyChar  = 0;      // unicode codepoint typed this frame
    bool  keyBackspace= false;
    bool  keyDelete   = false;
    bool  keyLeft     = false;
    bool  keyRight    = false;
    bool  keyHome     = false;
    bool  keyEnd      = false;
    bool  keyEnter    = false;
    bool  keyEscape   = false;
    bool  keyCtrlA    = false;
    bool  keyCtrlC    = false;
    bool  keyCtrlV    = false;
    bool  keyCtrlZ    = false;
};

// ── Panel descriptor for layout ───────────────────────────────────────────────
struct PanelDesc {
    std::string id;         // unique stable id (used for layout save)
    std::string title;      // display name on tab
    int         dockSlot;  // 0=left, 1=centre, 2=right, 3=bottom
};

} // namespace Demon::GUI