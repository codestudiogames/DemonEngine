#pragma once
// ==============================================================================
//  DemonEngine::DX12DescriptorHeap
//  Simple linear descriptor allocator for DX12.
// ==============================================================================
#include "core/DemonPCH.h"

namespace Demon {

class DX12DescriptorHeap {
public:
    void init(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t count, bool shaderVisible);
    void reset();

    [[nodiscard]] uint32_t allocate(uint32_t count = 1);
    [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle(uint32_t index) const;
    [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle(uint32_t index) const;
    [[nodiscard]] ID3D12DescriptorHeap* get() const { return m_heap.Get(); }
    [[nodiscard]] uint32_t getIncrement() const { return m_increment; }

private:
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_heap;
    D3D12_DESCRIPTOR_HEAP_TYPE m_type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    uint32_t m_capacity = 0;
    uint32_t m_allocated = 0;
    uint32_t m_increment = 0;
};

} // namespace Demon
