// =============================================================================
//  DemonGUI::GUIDrawList  —  Implementation
// =============================================================================
#include "GUIDrawList.h"
#include <cmath>

namespace Demon::GUI {

void GUIDrawList::reset()
{
    m_vtx.clear();
    m_idx.clear();
    m_cmds.clear();
    m_clipStack.clear();
    m_lastTex    = {};
    m_lastUseTex = false;
}

void GUIDrawList::reserve(uint32_t vtxCount, uint32_t idxCount)
{
    m_vtx.reserve(vtxCount);
    m_idx.reserve(idxCount);
}

void GUIDrawList::pushClip(Rect r) { m_clipStack.push_back({r}); }
void GUIDrawList::popClip()        { if (!m_clipStack.empty()) m_clipStack.pop_back(); }
Rect GUIDrawList::currentClip() const
{
    return m_clipStack.empty() ? Rect{0,0,65535,65535} : m_clipStack.back().rect;
}

// ── Internal helpers ──────────────────────────────────────────────────────────
void GUIDrawList::beginCmd(GUITextureID tex, bool useTex)
{
    bool sameCmd = !m_cmds.empty()
                && m_cmds.back().textureId.ptr == tex.ptr
                && m_cmds.back().useTexture    == useTex
                && m_cmds.back().clipRect.x    == currentClip().x
                && m_cmds.back().clipRect.y    == currentClip().y
                && m_cmds.back().clipRect.w    == currentClip().w
                && m_cmds.back().clipRect.h    == currentClip().h;
    if (!sameCmd) {
        DrawCmd cmd;
        cmd.indexOffset = static_cast<uint32_t>(m_idx.size());
        cmd.indexCount  = 0;
        cmd.clipRect    = currentClip();
        cmd.textureId   = tex;
        cmd.useTexture  = useTex;
        m_cmds.push_back(cmd);
    }
    m_lastTex    = tex;
    m_lastUseTex = useTex;
}

void GUIDrawList::pushQuad(Vec2 tl, Vec2 tr, Vec2 br, Vec2 bl,
                            Vec2 uvTL, Vec2 uvTR, Vec2 uvBR, Vec2 uvBL,
                            Color cTL, Color cTR, Color cBR, Color cBL,
                            GUITextureID tex, bool useTex)
{
    beginCmd(tex, useTex);
    uint32_t base = static_cast<uint32_t>(m_vtx.size());
    m_vtx.push_back({tl, uvTL, cTL.toRGBA8()});
    m_vtx.push_back({tr, uvTR, cTR.toRGBA8()});
    m_vtx.push_back({br, uvBR, cBR.toRGBA8()});
    m_vtx.push_back({bl, uvBL, cBL.toRGBA8()});
    m_idx.insert(m_idx.end(), {base,base+1,base+2, base,base+2,base+3});
    m_cmds.back().indexCount += 6;
}

// ── Public draw calls ─────────────────────────────────────────────────────────
void GUIDrawList::addRectFilled(Rect r, Color col, float /*rounding*/)
{
    Vec2 tl{r.x,     r.y};
    Vec2 tr{r.x+r.w, r.y};
    Vec2 br{r.x+r.w, r.y+r.h};
    Vec2 bl{r.x,     r.y+r.h};
    Vec2 uv{0,0};
    pushQuad(tl,tr,br,bl, uv,uv,uv,uv, col,col,col,col, {}, false);
}

void GUIDrawList::addRectGrad(Rect r, Color colTop, Color colBot)
{
    Vec2 tl{r.x,     r.y};
    Vec2 tr{r.x+r.w, r.y};
    Vec2 br{r.x+r.w, r.y+r.h};
    Vec2 bl{r.x,     r.y+r.h};
    Vec2 uv{0,0};
    pushQuad(tl,tr,br,bl, uv,uv,uv,uv, colTop,colTop,colBot,colBot, {}, false);
}

void GUIDrawList::addRect(Rect r, Color col, float /*rounding*/)
{
    // Four thin quads for the border (1px)
    addRectFilled({r.x,        r.y,        r.w, 1},   col);
    addRectFilled({r.x,        r.y+r.h-1,  r.w, 1},   col);
    addRectFilled({r.x,        r.y+1,      1, r.h-2}, col);
    addRectFilled({r.x+r.w-1,  r.y+1,      1, r.h-2}, col);
}

void GUIDrawList::addLine(Vec2 a, Vec2 b, Color col, float thickness)
{
    float dx = b.x - a.x, dy = b.y - a.y;
    float len = std::sqrt(dx*dx+dy*dy);
    if (len < 0.0001f) return;
    float nx = -dy/len * thickness*0.5f;
    float ny =  dx/len * thickness*0.5f;
    Vec2 tl{a.x+nx, a.y+ny}, tr{b.x+nx, b.y+ny};
    Vec2 br{b.x-nx, b.y-ny}, bl{a.x-nx, a.y-ny};
    Vec2 uv{};
    pushQuad(tl,tr,br,bl, uv,uv,uv,uv, col,col,col,col, {}, false);
}

void GUIDrawList::addTriangleFilled(Vec2 a, Vec2 b, Vec2 c, Color col)
{
    beginCmd({}, false);
    uint32_t base = static_cast<uint32_t>(m_vtx.size());
    Vec2 uv{};
    uint32_t packed = col.toRGBA8();
    m_vtx.push_back({a, uv, packed});
    m_vtx.push_back({b, uv, packed});
    m_vtx.push_back({c, uv, packed});
    m_idx.insert(m_idx.end(), {base, base+1, base+2});
    m_cmds.back().indexCount += 3;
}

void GUIDrawList::addImage(GUITextureID tex, Rect r, Color tint)
{
    addImageUV(tex, r, {0,0}, {1,1}, tint);
}

void GUIDrawList::addImageUV(GUITextureID tex, Rect r, Vec2 uvMin, Vec2 uvMax, Color tint)
{
    Vec2 tl{r.x,     r.y};
    Vec2 tr{r.x+r.w, r.y};
    Vec2 br{r.x+r.w, r.y+r.h};
    Vec2 bl{r.x,     r.y+r.h};
    pushQuad(tl, tr, br, bl,
             uvMin, {uvMax.x,uvMin.y}, uvMax, {uvMin.x,uvMax.y},
             tint, tint, tint, tint,
             tex, true);
}

void GUIDrawList::addGlyph(Rect dest, Vec2 uvMin, Vec2 uvMax, Color col)
{
    Vec2 tl{dest.x,        dest.y};
    Vec2 tr{dest.x+dest.w, dest.y};
    Vec2 br{dest.x+dest.w, dest.y+dest.h};
    Vec2 bl{dest.x,        dest.y+dest.h};
    // font uses the font-atlas texture (ptr==0 sentinel, renderer knows)
    pushQuad(tl, tr, br, bl,
             uvMin, {uvMax.x,uvMin.y}, uvMax, {uvMin.x,uvMax.y},
             col, col, col, col,
             {}, true);   // ptr==0 → renderer binds font atlas
}

} // namespace Demon::GUI
