// =============================================================================
//  DemonGUI::GUIRenderer  —  DX12 flush implementation
// =============================================================================
#include "GUIRenderer.h"
#include "renderer/DX12Context.h"
#include "renderer/DX12DescriptorHeap.h"
#include "renderer/Texture.h"
#include "core/Logger.h"
#include "core/PackageIO.h"
#include <directx/d3dx12.h>

namespace Demon::GUI {

static std::vector<uint8_t> readFile(const std::string& path)
{
    return PackageIO::readRuntimeBinary(path);
}

void GUIRenderer::init(DX12Context& ctx,
                       DX12DescriptorHeap& srvHeap,
                       DX12DescriptorHeap& samplerHeap,
                       DXGI_FORMAT rtvFormat,
                       uint32_t framesInFlight)
{
    m_ctx          = &ctx;
    m_srvHeap      = &srvHeap;
    m_samplerHeap  = &samplerHeap;
    m_framesInFlight = framesInFlight;
    m_frames.resize(framesInFlight);

    // Linear clamp sampler
    D3D12_SAMPLER_DESC sd{};
    sd.Filter   = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sd.MaxLOD   = D3D12_FLOAT32_MAX;
    m_linearSamplerIndex = m_samplerHeap->allocate(1);
    ctx.getDevice()->CreateSampler(&sd, m_samplerHeap->cpuHandle(m_linearSamplerIndex));

    m_whiteTexture = Demon::Texture::createWhite1x1(ctx, srvHeap);

    createRootSignature();
    createPipeline(rtvFormat);

    DEMON_LOG_INFO("GUIRenderer: DX12 backend initialised.");
}

void GUIRenderer::shutdown()
{
    for (auto& f : m_frames) {
        if (f.vb) { f.vb->Unmap(0, nullptr); f.vb.Reset(); }
        if (f.ib) { f.ib->Unmap(0, nullptr); f.ib.Reset(); }
    }
    m_pso.Reset();
    m_rootSig.Reset();
    m_whiteTexture.reset();
    m_ctx = nullptr;
}

void GUIRenderer::createRootSignature()
{
    // Slot 0: SRV table (1 texture)
    // Slot 1: Sampler table (1 sampler)
    // Slot 2: 32-bit constants (proj matrix: 4 floats)
    CD3DX12_DESCRIPTOR_RANGE srvRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,     1, 0);
    CD3DX12_DESCRIPTOR_RANGE sampRange(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, 0);

    CD3DX12_ROOT_PARAMETER params[3];
    params[0].InitAsDescriptorTable(1, &srvRange,  D3D12_SHADER_VISIBILITY_PIXEL);
    params[1].InitAsDescriptorTable(1, &sampRange, D3D12_SHADER_VISIBILITY_PIXEL);
    params[2].InitAsConstants(4, 0, 0, D3D12_SHADER_VISIBILITY_VERTEX); // proj scale+bias

    CD3DX12_ROOT_SIGNATURE_DESC desc{};
    desc.Init(_countof(params), params, 0, nullptr,
              D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    Microsoft::WRL::ComPtr<ID3DBlob> sig, err;
    HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err);
    DEMON_ASSERT(SUCCEEDED(hr), "GUIRenderer: failed to serialize root signature");
    hr = m_ctx->getDevice()->CreateRootSignature(0, sig->GetBufferPointer(),
                                                  sig->GetBufferSize(),
                                                  IID_PPV_ARGS(&m_rootSig));
    DEMON_ASSERT(SUCCEEDED(hr), "GUIRenderer: failed to create root signature");
}

void GUIRenderer::createPipeline(DXGI_FORMAT rtvFormat)
{
    auto vs = readFile("assets/shaders/dx12/gui_vs.cso");
    auto ps = readFile("assets/shaders/dx12/gui_ps.cso");
    if (vs.empty() || ps.empty()) {
        DEMON_LOG_ERROR("GUIRenderer: missing gui_vs.cso or gui_ps.cso — GUI will not render.");
        return;
    }

    D3D12_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,    0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0,  8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM,  0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    // Alpha blend
    D3D12_RENDER_TARGET_BLEND_DESC rtBlend{};
    rtBlend.BlendEnable    = TRUE;
    rtBlend.SrcBlend       = D3D12_BLEND_SRC_ALPHA;
    rtBlend.DestBlend      = D3D12_BLEND_INV_SRC_ALPHA;
    rtBlend.BlendOp        = D3D12_BLEND_OP_ADD;
    rtBlend.SrcBlendAlpha  = D3D12_BLEND_ONE;
    rtBlend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    rtBlend.BlendOpAlpha   = D3D12_BLEND_OP_ADD;
    rtBlend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.InputLayout                   = {layout, _countof(layout)};
    pso.pRootSignature                = m_rootSig.Get();
    pso.VS                            = {vs.data(), vs.size()};
    pso.PS                            = {ps.data(), ps.size()};
    pso.RasterizerState               = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    pso.RasterizerState.CullMode      = D3D12_CULL_MODE_NONE;
    // Note: DX12 has no ScissorEnable in D3D12_RASTERIZER_DESC — scissor rects
    // are always active and are set per-draw via RSSetScissorRects on the command list.
    pso.BlendState                    = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    pso.BlendState.RenderTarget[0]    = rtBlend;
    pso.DepthStencilState             = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    pso.DepthStencilState.DepthEnable = FALSE;
    pso.SampleMask                    = UINT_MAX;
    pso.PrimitiveTopologyType         = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets              = 1;
    pso.RTVFormats[0]                 = rtvFormat;
    pso.DSVFormat                     = DXGI_FORMAT_UNKNOWN;
    pso.SampleDesc.Count              = 1;

    HRESULT hr = m_ctx->getDevice()->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_pso));
    DEMON_ASSERT(SUCCEEDED(hr), "GUIRenderer: failed to create PSO");
}

void GUIRenderer::ensureBuffers(uint32_t fi, uint32_t vtxCount, uint32_t idxCount)
{
    auto& fb = m_frames[fi];
    auto device = m_ctx->getDevice();

    auto grow = [&](Microsoft::WRL::ComPtr<ID3D12Resource>& buf,
                    uint8_t*& mapped, uint32_t& cap,
                    uint32_t needed, uint32_t stride, const wchar_t* name)
    {
        if (cap >= needed) return;
        if (buf) { buf->Unmap(0, nullptr); buf.Reset(); }
        cap = needed + needed / 2;   // 1.5x growth
        auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        auto bufDesc   = CD3DX12_RESOURCE_DESC::Buffer(uint64_t(cap) * stride);
        device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &bufDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&buf));
        buf->SetName(name);
        CD3DX12_RANGE r(0,0);
        buf->Map(0, &r, reinterpret_cast<void**>(&mapped));
    };

    grow(fb.vb, fb.vbMapped, fb.vbCapacity, vtxCount, sizeof(GUIVertex), L"GUI_VB");
    grow(fb.ib, fb.ibMapped, fb.ibCapacity, idxCount, sizeof(uint32_t),   L"GUI_IB");
}

void GUIRenderer::flush(ID3D12GraphicsCommandList* cmd,
                        const GUIDrawList& dl,
                        uint32_t frameIndex,
                        float displayW, float displayH)
{
    if (!m_pso || !m_rootSig || !cmd) return;
    const auto& vtx = dl.vertices();
    const auto& idx = dl.indices();
    const auto& cmds = dl.cmds();
    if (vtx.empty() || cmds.empty()) return;

    ensureBuffers(frameIndex,
                  static_cast<uint32_t>(vtx.size()),
                  static_cast<uint32_t>(idx.size()));

    auto& fb = m_frames[frameIndex];
    std::memcpy(fb.vbMapped, vtx.data(), vtx.size() * sizeof(GUIVertex));
    std::memcpy(fb.ibMapped, idx.data(), idx.size() * sizeof(uint32_t));

    // Full-screen viewport (GUI is always rendered over the backbuffer)
    D3D12_VIEWPORT vp{0, 0, displayW, displayH, 0, 1};
    cmd->RSSetViewports(1, &vp);

    cmd->SetPipelineState(m_pso.Get());
    cmd->SetGraphicsRootSignature(m_rootSig.Get());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Projection: map [0..displayW] × [0..displayH] → [-1..1] × [1..-1]
    // Passed as 4 constants: scaleX, scaleY, biasX, biasY
    float proj[4] = {
         2.f / displayW,
        -2.f / displayH,
        -1.f,
         1.f
    };
    cmd->SetGraphicsRoot32BitConstants(2, 4, proj, 0);

    // Sampler
    cmd->SetGraphicsRootDescriptorTable(1,
        m_samplerHeap->gpuHandle(m_linearSamplerIndex));

    // VB / IB
    D3D12_VERTEX_BUFFER_VIEW vbv{};
    vbv.BufferLocation = fb.vb->GetGPUVirtualAddress();
    vbv.SizeInBytes    = static_cast<UINT>(vtx.size() * sizeof(GUIVertex));
    vbv.StrideInBytes  = sizeof(GUIVertex);
    cmd->IASetVertexBuffers(0, 1, &vbv);

    D3D12_INDEX_BUFFER_VIEW ibv{};
    ibv.BufferLocation = fb.ib->GetGPUVirtualAddress();
    ibv.SizeInBytes    = static_cast<UINT>(idx.size() * sizeof(uint32_t));
    ibv.Format         = DXGI_FORMAT_R32_UINT;
    cmd->IASetIndexBuffer(&ibv);

    // Draw commands
    for (const auto& dc : cmds) {
        if (dc.indexCount == 0) continue;

        // Scissor
        D3D12_RECT sc{
            static_cast<LONG>(dc.clipRect.x),
            static_cast<LONG>(dc.clipRect.y),
            static_cast<LONG>(dc.clipRect.x + dc.clipRect.w),
            static_cast<LONG>(dc.clipRect.y + dc.clipRect.h)
        };
        cmd->RSSetScissorRects(1, &sc);

        // Bind a valid texture for every draw. Solid-color primitives use a 1x1
        // white texture so the pixel shader can still tint correctly.
        D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle{};
        if (!dc.useTexture) {
            gpuHandle = m_whiteTexture ? m_whiteTexture->getSrvGpuHandle() : D3D12_GPU_DESCRIPTOR_HANDLE{};
        } else if (dc.textureId.ptr == 0 && m_font) {
            gpuHandle = m_font->getAtlasGpuHandle();
        } else {
            gpuHandle = dc.textureId;
        }
        cmd->SetGraphicsRootDescriptorTable(0, gpuHandle);

        cmd->DrawIndexedInstanced(dc.indexCount, 1, dc.indexOffset, 0, 0);
    }
}

} // namespace Demon::GUI
