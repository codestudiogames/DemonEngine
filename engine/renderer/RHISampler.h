#pragma once
// ============================================================================
//  DemonEngine::RHISampler
//  Minimal render-hardware abstraction for sampler descriptors (DX12-backed).
// ============================================================================
#include "core/DemonPCH.h"

namespace Demon {

class DX12DescriptorHeap;

class RHISampler {
public:
    RHISampler() = default;

    // Main-thread-only: create sampler descriptor.
    void create(ID3D12Device* device, DX12DescriptorHeap& samplerHeap, const D3D12_SAMPLER_DESC& desc);

    [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE getCpuHandle() const { return m_cpu; }
    [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE getGpuHandle() const { return m_gpu; }
    [[nodiscard]] bool isValid() const { return m_index != UINT32_MAX; }

private:
    uint32_t m_index = UINT32_MAX;
    D3D12_CPU_DESCRIPTOR_HANDLE m_cpu{};
    D3D12_GPU_DESCRIPTOR_HANDLE m_gpu{};
};

} // namespace Demon
