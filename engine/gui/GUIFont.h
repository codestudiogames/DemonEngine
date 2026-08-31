#pragma once
// =============================================================================
//  DemonGUI::GUIFont  —  stb_truetype font rasterizer + GPU atlas
// =============================================================================
#include "GUITypes.h"
#include "GUIDrawList.h"

// Forward-declare only — stb_truetype.h is included in GUIFont.cpp where
// STB_TRUETYPE_IMPLEMENTATION is defined. Packed glyph data is stored as bytes
// so this header never needs the complete stbtt_packedchar type.
struct stbtt_packedchar;

namespace Demon {
class DX12Context;
class DX12DescriptorHeap;
}

namespace Demon::GUI {

struct GlyphInfo {
    float x0, y0, x1, y1;      // pixel coords in atlas
    float u0, v0, u1, v1;      // UV coords
    float xAdvance;
    float xOff, yOff;
};

class GUIFont {
public:
    GUIFont() = default;
    ~GUIFont();  // defined in GUIFont.cpp — needs complete stbtt_packedchar

    // Load from embedded TTF bytes (pass nullptr to use built-in Proggy font)
    bool loadEmbedded(float pixelHeight,
                      DX12Context& ctx,
                      DX12DescriptorHeap& srvHeap);

    bool loadFromFile(const std::string& path, float pixelHeight,
                      DX12Context& ctx,
                      DX12DescriptorHeap& srvHeap);

    // Submit a text string to a draw list. `scale` uniformly scales glyph
    // quads/advances so one atlas serves any on-screen size.
    void drawText(GUIDrawList& dl, Vec2 pos, const std::string& text,
                  Color col, float wrapWidth = 0.f, float scale = 1.f) const;

    // Measure text extents without drawing
    [[nodiscard]] Vec2 measureText(const std::string& text) const;

    [[nodiscard]] float       lineHeight()    const { return m_lineHeight; }
    [[nodiscard]] bool        isLoaded()      const { return m_loaded; }

    // GPU atlas SRV — renderer binds this when DrawCmd::textureId.ptr == 0
    [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE getAtlasCpuHandle() const { return m_srvCpu; }
    [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE getAtlasGpuHandle() const { return m_srvGpu; }

    void destroy();

private:
    bool buildAtlas(const uint8_t* ttfData, size_t ttfSize, float pixelHeight,
                    DX12Context& ctx, DX12DescriptorHeap& srvHeap);

    static constexpr int k_atlasW  = 512;
    static constexpr int k_atlasH  = 512;
    static constexpr int k_firstChar = 32;
    static constexpr int k_charCount = 96;   // ASCII printable

    std::vector<GlyphInfo> m_glyphs;
    std::unique_ptr<uint8_t[]> m_packedData;
    float m_lineHeight = 16.f;
    float m_ascent     = 12.f;
    bool  m_loaded     = false;

    Microsoft::WRL::ComPtr<ID3D12Resource> m_atlasResource;
    D3D12_CPU_DESCRIPTOR_HANDLE m_srvCpu{};
    D3D12_GPU_DESCRIPTOR_HANDLE m_srvGpu{};
    uint32_t m_srvIndex = UINT32_MAX;
};

} // namespace Demon::GUI
