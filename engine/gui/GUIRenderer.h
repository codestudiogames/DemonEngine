#pragma once
// =============================================================================
//  DemonGUI::GUIRenderer  —  DX12 backend for GUIDrawList
//  One PSO, one root signature, dynamic ring-buffer VB/IB.
// =============================================================================
#include "GUITypes.h"
#include "GUIDrawList.h"
#include "GUIFont.h"

namespace Demon {
class DX12Context;
class DX12DescriptorHeap;
class Texture;
}

namespace Demon::GUI {

class GUIRenderer {
public:
    GUIRenderer() = default;
    ~GUIRenderer() { shutdown(); }

    void init(DX12Context& ctx,
              DX12DescriptorHeap& srvHeap,
              DX12DescriptorHeap& samplerHeap,
              DXGI_FORMAT rtvFormat,
              uint32_t framesInFlight);

    void shutdown();

    // Call once after init to register the font atlas texture
    void setFont(GUIFont* font) { m_font = font; }

    // Call inside the frame command list — after scene, before present
    void flush(ID3D12GraphicsCommandList* cmd,
               const GUIDrawList& dl,
               uint32_t frameIndex,
               float displayW, float displayH);

    void onResize(float w, float h) { m_displayW = w; m_displayH = h; }

private:
    void createRootSignature();
    void createPipeline(DXGI_FORMAT rtvFormat);
    void ensureBuffers(uint32_t frameIndex, uint32_t vtxCount, uint32_t idxCount);

    DX12Context*        m_ctx         = nullptr;
    DX12DescriptorHeap* m_srvHeap     = nullptr;
    DX12DescriptorHeap* m_samplerHeap = nullptr;
    GUIFont*            m_font        = nullptr;
    std::shared_ptr<Demon::Texture> m_whiteTexture;
    uint32_t            m_framesInFlight = 2;
    float               m_displayW    = 1280.f;
    float               m_displayH    = 720.f;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rootSig;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pso;

    struct FrameBuffer {
        Microsoft::WRL::ComPtr<ID3D12Resource> vb;
        Microsoft::WRL::ComPtr<ID3D12Resource> ib;
        uint8_t* vbMapped = nullptr;
        uint8_t* ibMapped = nullptr;
        uint32_t vbCapacity = 0;
        uint32_t ibCapacity = 0;
    };
    std::vector<FrameBuffer> m_frames;

    uint32_t m_linearSamplerIndex = UINT32_MAX;
};

} // namespace Demon::GUI
