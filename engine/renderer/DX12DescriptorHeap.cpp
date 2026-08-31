#include "DX12DescriptorHeap.h"
#include "core/Logger.h"

namespace Demon {

void DX12DescriptorHeap::init(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t count, bool shaderVisible)
{
    m_type = type;
    m_capacity = count;
    m_allocated = 0;

    D3D12_DESCRIPTOR_HEAP_DESC desc{};
    desc.Type = type;
    desc.NumDescriptors = count;
    desc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    desc.NodeMask = 0;

    HRESULT hr = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_heap));
    DEMON_ASSERT(SUCCEEDED(hr), "Failed to create descriptor heap");
    m_increment = device->GetDescriptorHandleIncrementSize(type);
}

void DX12DescriptorHeap::reset()
{
    m_allocated = 0;
}

uint32_t DX12DescriptorHeap::allocate(uint32_t count)
{
    DEMON_ASSERT(m_allocated + count <= m_capacity, "DX12DescriptorHeap out of space");
    uint32_t index = m_allocated;
    m_allocated += count;
    return index;
}

D3D12_CPU_DESCRIPTOR_HANDLE DX12DescriptorHeap::cpuHandle(uint32_t index) const
{
    D3D12_CPU_DESCRIPTOR_HANDLE h{};
#if defined(__MINGW32__)
    m_heap->GetCPUDescriptorHandleForHeapStart(&h);
#else
    h = m_heap->GetCPUDescriptorHandleForHeapStart();
#endif
    h.ptr += static_cast<SIZE_T>(index) * m_increment;
    return h;
}

D3D12_GPU_DESCRIPTOR_HANDLE DX12DescriptorHeap::gpuHandle(uint32_t index) const
{
    D3D12_GPU_DESCRIPTOR_HANDLE h{};
#if defined(__MINGW32__)
    m_heap->GetGPUDescriptorHandleForHeapStart(&h);
#else
    h = m_heap->GetGPUDescriptorHandleForHeapStart();
#endif
    h.ptr += static_cast<UINT64>(index) * m_increment;
    return h;
}

} // namespace Demon
