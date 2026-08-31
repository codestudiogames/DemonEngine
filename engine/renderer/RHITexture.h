#pragma once
// ============================================================================
//  DemonEngine::RHITexture
//  Minimal render-hardware abstraction for GPU textures (DX12-backed).
// ============================================================================
#include "core/DemonPCH.h"

namespace Demon {

class DX12DescriptorHeap;

struct RHITextureDesc {
    uint32_t width = 1;
    uint32_t height = 1;
    DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
    uint16_t mipLevels = 1;
    D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
    D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON;
    bool createSRV = true;
    bool createRTV = false;
    bool createDSV = false;
    bool hasClearValue = false;
    D3D12_CLEAR_VALUE clearValue{};
    DXGI_FORMAT srvFormatOverride = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT rtvFormatOverride = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT dsvFormatOverride = DXGI_FORMAT_UNKNOWN;
};

class RHITexture {
public:
    RHITexture() = default;
    ~RHITexture() { destroy(); }

    // Main-thread-only: create or recreate the resource.
    void create(ID3D12Device* device,
                const RHITextureDesc& desc,
                DX12DescriptorHeap* rtvHeap,
                DX12DescriptorHeap* dsvHeap,
                DX12DescriptorHeap* srvHeap,
                const char* debugName = nullptr);

    // Main-thread-only: release the GPU resource (descriptor slots are kept).
    void destroy();

    // Render-thread-safe: record a transition barrier if needed.
    void transition(ID3D12GraphicsCommandList* cmd, D3D12_RESOURCE_STATES newState);

    [[nodiscard]] ID3D12Resource* getResource() const { return m_resource.Get(); }
    [[nodiscard]] uint32_t getWidth() const { return m_width; }
    [[nodiscard]] uint32_t getHeight() const { return m_height; }
    [[nodiscard]] DXGI_FORMAT getFormat() const { return m_format; }
    [[nodiscard]] D3D12_RESOURCE_STATES getState() const { return m_state; }
    [[nodiscard]] bool isValid() const { return m_resource != nullptr; }

    [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE getRtv() const { return m_rtv; }
    [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE getDsv() const { return m_dsv; }
    [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE getSrvCpu() const { return m_srvCpu; }
    [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE getSrvGpu() const { return m_srvGpu; }

private:
    void updateRtv(ID3D12Device* device, DX12DescriptorHeap* rtvHeap, const RHITextureDesc& desc);
    void updateDsv(ID3D12Device* device, DX12DescriptorHeap* dsvHeap, const RHITextureDesc& desc);
    void updateSrv(ID3D12Device* device, DX12DescriptorHeap* srvHeap, const RHITextureDesc& desc);

    Microsoft::WRL::ComPtr<ID3D12Resource> m_resource;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    DXGI_FORMAT m_format = DXGI_FORMAT_UNKNOWN;
    D3D12_RESOURCE_STATES m_state = D3D12_RESOURCE_STATE_COMMON;

    uint32_t m_rtvIndex = UINT32_MAX;
    uint32_t m_dsvIndex = UINT32_MAX;
    uint32_t m_srvIndex = UINT32_MAX;
    D3D12_CPU_DESCRIPTOR_HANDLE m_rtv{};
    D3D12_CPU_DESCRIPTOR_HANDLE m_dsv{};
    D3D12_CPU_DESCRIPTOR_HANDLE m_srvCpu{};
    D3D12_GPU_DESCRIPTOR_HANDLE m_srvGpu{};
};

} // namespace Demon
