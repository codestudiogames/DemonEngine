// =============================================================================
//  DemonGUI::GUIFont  —  Implementation
// =============================================================================
#include "GUIFont.h"
#include "renderer/DX12Context.h"
#include "renderer/DX12DescriptorHeap.h"
#include "core/Logger.h"
#include <directx/d3dx12.h>
#include <fstream>

// stb_truetype implementation — compiled exactly once here.
// Must appear after GUIFont.h (which only forward-declares stbtt_packedchar).
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

// ── Embedded Proggy Clean font (binary blob) ──────────────────────────────────
// This is a minimal ~6 KB subset embedded as a C array.
// Replace with the full ProggyClean.ttf bytes for production.
extern const uint8_t  g_ProggyClean_ttf[];
extern const uint32_t g_ProggyClean_ttf_size;

namespace Demon::GUI {

static stbtt_packedchar* packedChars(std::unique_ptr<uint8_t[]>& packedData)
{
    return reinterpret_cast<stbtt_packedchar*>(packedData.get());
}

static const stbtt_packedchar* packedChars(const std::unique_ptr<uint8_t[]>& packedData)
{
    return reinterpret_cast<const stbtt_packedchar*>(packedData.get());
}

// Destructor stays out-of-line so all atlas teardown remains in one TU.
GUIFont::~GUIFont() { destroy(); }

bool GUIFont::loadEmbedded(float pixelHeight,
                           DX12Context& ctx,
                           DX12DescriptorHeap& srvHeap)
{
    return buildAtlas(g_ProggyClean_ttf, g_ProggyClean_ttf_size,
                      pixelHeight, ctx, srvHeap);
}

bool GUIFont::loadFromFile(const std::string& path, float pixelHeight,
                           DX12Context& ctx, DX12DescriptorHeap& srvHeap)
{
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) {
        DEMON_LOG_WARN("GUIFont: cannot open '{}', falling back to embedded font.", path);
        return loadEmbedded(pixelHeight, ctx, srvHeap);
    }
    f.seekg(0, std::ios::end);
    size_t sz = static_cast<size_t>(f.tellg());
    f.seekg(0);
    std::vector<uint8_t> buf(sz);
    f.read(reinterpret_cast<char*>(buf.data()), sz);
    return buildAtlas(buf.data(), sz, pixelHeight, ctx, srvHeap);
}

bool GUIFont::buildAtlas(const uint8_t* ttfData, size_t ttfSize,
                         float pixelHeight,
                         DX12Context& ctx, DX12DescriptorHeap& srvHeap)
{
    // Reject empty/stub font blobs (the embedded Proggy placeholder is empty)
    // before stb_truetype reads out of bounds.
    stbtt_fontinfo probe{};
    if (!ttfData || ttfSize < 12 || !stbtt_InitFont(&probe, ttfData, 0)) {
        DEMON_LOG_WARN("GUIFont: invalid or empty TTF data ({} bytes) — font not loaded.", ttfSize);
        return false;
    }

    m_lineHeight = pixelHeight;

    // Rasterize glyphs into a greyscale bitmap
    std::vector<uint8_t> bitmap(k_atlasW * k_atlasH, 0);
    m_packedData = std::make_unique<uint8_t[]>(sizeof(stbtt_packedchar) * k_charCount);
    auto* packed = packedChars(m_packedData);

    stbtt_pack_context pc{};
    stbtt_PackBegin(&pc, bitmap.data(), k_atlasW, k_atlasH, 0, 1, nullptr);
    stbtt_PackSetOversampling(&pc, 2, 2);
    stbtt_PackFontRange(&pc, ttfData, 0, pixelHeight,
                        k_firstChar, k_charCount, packed);
    stbtt_PackEnd(&pc);

    // Compute ascent for baseline alignment
    stbtt_fontinfo fi{};
    stbtt_InitFont(&fi, ttfData, 0);
    int ascent = 0, descent = 0, lineGap = 0;
    stbtt_GetFontVMetrics(&fi, &ascent, &descent, &lineGap);
    float scale = stbtt_ScaleForPixelHeight(&fi, pixelHeight);
    m_ascent = static_cast<float>(ascent) * scale;

    // Build per-glyph info
    m_glyphs.resize(k_charCount);
    for (int i = 0; i < k_charCount; ++i) {
        const auto& pc2 = packed[i];
        GlyphInfo& g = m_glyphs[i];
        g.x0 = pc2.x0;  g.y0 = pc2.y0;
        g.x1 = pc2.x1;  g.y1 = pc2.y1;
        g.u0 = pc2.x0 / float(k_atlasW);  g.v0 = pc2.y0 / float(k_atlasH);
        g.u1 = pc2.x1 / float(k_atlasW);  g.v1 = pc2.y1 / float(k_atlasH);
        g.xAdvance = pc2.xadvance;
        g.xOff = pc2.xoff;  g.yOff = pc2.yoff;
    }

    // Convert greyscale → RGBA8 for DX12 (R channel = alpha)
    std::vector<uint8_t> rgba(k_atlasW * k_atlasH * 4, 0);
    for (int i = 0; i < k_atlasW * k_atlasH; ++i) {
        rgba[i*4+0] = 255;
        rgba[i*4+1] = 255;
        rgba[i*4+2] = 255;
        rgba[i*4+3] = bitmap[i];
    }

    // Upload to GPU
    auto device = ctx.getDevice();
    auto texDesc = CD3DX12_RESOURCE_DESC::Tex2D(
        DXGI_FORMAT_R8G8B8A8_UNORM, k_atlasW, k_atlasH, 1, 1);
    auto heapDefault = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    HRESULT hr = device->CreateCommittedResource(
        &heapDefault, D3D12_HEAP_FLAG_NONE, &texDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
        IID_PPV_ARGS(&m_atlasResource));
    if (FAILED(hr)) {
        DEMON_LOG_ERROR("GUIFont: failed to create atlas texture.");
        return false;
    }
    m_atlasResource->SetName(L"GUIFont_Atlas");

    // Upload via immediate submit
    UINT64 uploadSize = 0;
    device->GetCopyableFootprints(&texDesc, 0, 1, 0, nullptr, nullptr, nullptr, &uploadSize);
    auto heapUpload = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    auto uploadBufDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);
    Microsoft::WRL::ComPtr<ID3D12Resource> uploadBuf;
    device->CreateCommittedResource(
        &heapUpload, D3D12_HEAP_FLAG_NONE, &uploadBufDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadBuf));

    D3D12_SUBRESOURCE_DATA subData{};
    subData.pData = rgba.data();
    subData.RowPitch = k_atlasW * 4;
    subData.SlicePitch = k_atlasW * k_atlasH * 4;

    ctx.immediateSubmit([&](ID3D12GraphicsCommandList* cmd) {
        UpdateSubresources(cmd, m_atlasResource.Get(), uploadBuf.Get(), 0, 0, 1, &subData);
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            m_atlasResource.Get(),
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        cmd->ResourceBarrier(1, &barrier);
    });

    // SRV
    if (m_srvIndex == UINT32_MAX)
        m_srvIndex = srvHeap.allocate(1);
    m_srvCpu = srvHeap.cpuHandle(m_srvIndex);
    m_srvGpu = srvHeap.gpuHandle(m_srvIndex);

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels     = 1;
    device->CreateShaderResourceView(m_atlasResource.Get(), &srvDesc, m_srvCpu);

    m_loaded = true;
    DEMON_LOG_INFO("GUIFont: atlas built ({}x{}, {:.0f}px).", k_atlasW, k_atlasH, pixelHeight);
    return true;
}

void GUIFont::drawText(GUIDrawList& dl, Vec2 pos, const std::string& text,
                       Color col, float wrapWidth, float scale) const
{
    if (!m_loaded || scale <= 0.f) return;
    float cx = pos.x, cy = pos.y + m_ascent * scale;
    for (unsigned char c : text) {
        if (c == '\n') { cx = pos.x; cy += m_lineHeight * scale; continue; }
        if (c < k_firstChar || c >= k_firstChar + k_charCount) continue;
        if (wrapWidth > 0.f && cx - pos.x + m_glyphs[c - k_firstChar].xAdvance * scale > wrapWidth) {
            cx = pos.x; cy += m_lineHeight * scale;
        }
        const GlyphInfo& g = m_glyphs[c - k_firstChar];
        Rect dest{ cx + g.xOff * scale, cy + g.yOff * scale,
                   (g.x1 - g.x0) * scale, (g.y1 - g.y0) * scale };
        dl.addGlyph(dest, {g.u0, g.v0}, {g.u1, g.v1}, col);
        cx += g.xAdvance * scale;
    }
}

Vec2 GUIFont::measureText(const std::string& text) const
{
    if (!m_loaded) return {};
    float maxW = 0.f, cx = 0.f, lines = 1.f;
    for (unsigned char c : text) {
        if (c == '\n') { maxW = std::max(maxW, cx); cx = 0.f; lines++; continue; }
        if (c < k_firstChar || c >= k_firstChar + k_charCount) continue;
        cx += m_glyphs[c - k_firstChar].xAdvance;
    }
    maxW = std::max(maxW, cx);
    return {maxW, lines * m_lineHeight};
}

void GUIFont::destroy()
{
    m_atlasResource.Reset();
    m_glyphs.clear();
    m_packedData.reset();
    m_loaded = false;
    m_srvCpu = {};
    m_srvGpu = {};
    m_srvIndex = UINT32_MAX;
}

} // namespace Demon::GUI
