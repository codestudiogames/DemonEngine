#pragma once
// =============================================================================
//  DemonGUI::GUIDrawList  —  CPU-side draw command buffer
//  All GUI widgets push commands here; GUIRenderer flushes to DX12.
// =============================================================================
#include "GUITypes.h"

namespace Demon::GUI {

// ── Vertex layout (must match GUI.hlsl) ──────────────────────────────────────
struct GUIVertex {
    Vec2     pos;
    Vec2     uv;
    uint32_t col;   // RGBA8 packed
};

// ── Draw command ──────────────────────────────────────────────────────────────
struct DrawCmd {
    uint32_t     indexOffset  = 0;
    uint32_t     indexCount   = 0;
    Rect         clipRect     {};
    GUITextureID textureId    {};   // GPU descriptor; .ptr==0 → font atlas
    bool         useTexture   = false;
};

// ── Scissor/clip stack entry ──────────────────────────────────────────────────
struct ClipEntry { Rect rect; };

class GUIDrawList {
public:
    GUIDrawList() { reserve(4096, 8192); }

    void reset();
    void reserve(uint32_t vtxCount, uint32_t idxCount);

    // ── Primitives ────────────────────────────────────────────────────────────
    void addRect        (Rect r, Color col, float rounding = 0.f);
    void addRectFilled  (Rect r, Color col, float rounding = 0.f);
    void addRectGrad    (Rect r, Color colTop, Color colBot);
    void addLine        (Vec2 a, Vec2 b, Color col, float thickness = 1.f);
    void addTriangleFilled(Vec2 a, Vec2 b, Vec2 c, Color col);
    void addImage       (GUITextureID tex, Rect r, Color tint = {1,1,1,1});
    void addImageUV     (GUITextureID tex, Rect r, Vec2 uvMin, Vec2 uvMax, Color tint = {1,1,1,1});

    // Text is submitted via GUIFont → addGlyph calls
    void addGlyph       (Rect dest, Vec2 uvMin, Vec2 uvMax, Color col);

    // ── Clip stack ────────────────────────────────────────────────────────────
    void pushClip(Rect r);
    void popClip();
    [[nodiscard]] Rect currentClip() const;

    // ── Data access for GUIRenderer ───────────────────────────────────────────
    [[nodiscard]] const std::vector<GUIVertex>& vertices() const { return m_vtx; }
    [[nodiscard]] const std::vector<uint32_t>&  indices()  const { return m_idx; }
    [[nodiscard]] const std::vector<DrawCmd>&   cmds()     const { return m_cmds; }

private:
    void beginCmd(GUITextureID tex, bool useTexture);
    void pushQuad(Vec2 tl, Vec2 tr, Vec2 br, Vec2 bl,
                  Vec2 uvTL, Vec2 uvTR, Vec2 uvBR, Vec2 uvBL,
                  Color colTL, Color colTR, Color colBR, Color colBL,
                  GUITextureID tex, bool useTex);

    std::vector<GUIVertex> m_vtx;
    std::vector<uint32_t>  m_idx;
    std::vector<DrawCmd>   m_cmds;
    std::vector<ClipEntry> m_clipStack;
    GUITextureID           m_lastTex   {};
    bool                   m_lastUseTex= false;
};

} // namespace Demon::GUI
