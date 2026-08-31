// ============================================================================
//  DemonEngine::RHITexture  -  DX12-backed texture implementation
// ============================================================================
#include "RHITexture.h"
#include "DX12DescriptorHeap.h"
#include "core/Logger.h"
#include <directx/d3dx12.h>

namespace Demon {

static std::wstring toWide(const char* name)
{
    if (!name) return {};
    size_t len = std::strlen(name);
    if (len == 0) return {};
    std::wstring w;
    w.resize(len);
    for (size_t i = 0; i < len; ++i)
        w[i] = static_cast<wchar_t>(name[i]);
    return w;
}

void RHITexture::create(ID3D12Device* device,
                        const RHITextureDesc& desc,
                        DX12DescriptorHeap* rtvHeap,
                        DX12DescriptorHeap* dsvHeap,
                        DX12DescriptorHeap* srvHeap,
                        const char* debugName)
{
    destroy();
    if (!device || desc.width == 0 || desc.height == 0) {
        DEMON_LOG_WARN("RHITexture: create called with invalid device or size.");
        return;
    }

    m_width = desc.width;
    m_height = desc.height;
    m_format = desc.format;
    m_state = desc.initialState;

    auto texDesc = CD3DX12_RESOURCE_DESC::Tex2D(
        desc.format,
        desc.width,
        desc.height,
        1,
        desc.mipLevels,
        1,
        0,
        desc.flags);

    auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    const D3D12_CLEAR_VALUE* clearPtr = desc.hasClearValue ? &desc.clearValue : nullptr;

    HRESULT hr = device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &texDesc,
        desc.initialState,
        clearPtr,
        IID_PPV_ARGS(&m_resource));
    DEMON_ASSERT(SUCCEEDED(hr), "Failed to create RHITexture");

    if (m_resource && debugName) {
        auto wide = toWide(debugName);
        if (!wide.empty())
            m_resource->SetName(wide.c_str());
    }

    if (desc.createRTV) updateRtv(device, rtvHeap, desc);
    if (desc.createDSV) updateDsv(device, dsvHeap, desc);
    if (desc.createSRV) updateSrv(device, srvHeap, desc);
}

void RHITexture::destroy()
{
    m_resource.Reset();
    m_width = 0;
    m_height = 0;
    m_format = DXGI_FORMAT_UNKNOWN;
    m_state = D3D12_RESOURCE_STATE_COMMON;
    m_rtv = {};
    m_dsv = {};
    m_srvCpu = {};
    m_srvGpu = {};
}

void RHITexture::transition(ID3D12GraphicsCommandList* cmd, D3D12_RESOURCE_STATES newState)
{
    if (!cmd || !m_resource || m_state == newState) return;
    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(m_resource.Get(), m_state, newState);
    cmd->ResourceBarrier(1, &barrier);
    m_state = newState;
}

void RHITexture::updateRtv(ID3D12Device* device, DX12DescriptorHeap* rtvHeap, const RHITextureDesc& desc)
{
    if (!device || !rtvHeap) return;
    if (m_rtvIndex == UINT32_MAX)
        m_rtvIndex = rtvHeap->allocate(1);
    m_rtv = rtvHeap->cpuHandle(m_rtvIndex);

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
    rtvDesc.Format = (desc.rtvFormatOverride == DXGI_FORMAT_UNKNOWN) ? desc.format : desc.rtvFormatOverride;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    rtvDesc.Texture2D.MipSlice = 0;
    rtvDesc.Texture2D.PlaneSlice = 0;
    device->CreateRenderTargetView(m_resource.Get(), &rtvDesc, m_rtv);
}

void RHITexture::updateDsv(ID3D12Device* device, DX12DescriptorHeap* dsvHeap, const RHITextureDesc& desc)
{
    if (!device || !dsvHeap) return;
    if (m_dsvIndex == UINT32_MAX)
        m_dsvIndex = dsvHeap->allocate(1);
    m_dsv = dsvHeap->cpuHandle(m_dsvIndex);

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
    dsvDesc.Format = (desc.dsvFormatOverride == DXGI_FORMAT_UNKNOWN) ? desc.format : desc.dsvFormatOverride;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
    device->CreateDepthStencilView(m_resource.Get(), &dsvDesc, m_dsv);
}

void RHITexture::updateSrv(ID3D12Device* device, DX12DescriptorHeap* srvHeap, const RHITextureDesc& desc)
{
    if (!device || !srvHeap) return;
    if (m_srvIndex == UINT32_MAX)
        m_srvIndex = srvHeap->allocate(1);
    m_srvCpu = srvHeap->cpuHandle(m_srvIndex);
    m_srvGpu = srvHeap->gpuHandle(m_srvIndex);

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = (desc.srvFormatOverride == DXGI_FORMAT_UNKNOWN) ? desc.format : desc.srvFormatOverride;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = desc.mipLevels;
    srvDesc.Texture2D.MostDetailedMip = 0;
    device->CreateShaderResourceView(m_resource.Get(), &srvDesc, m_srvCpu);
}

} // namespace Demon
