// ============================================================================
//  DemonEngine::RHISampler  -  DX12-backed sampler descriptor
// ============================================================================
#include "RHISampler.h"
#include "DX12DescriptorHeap.h"

namespace Demon {

void RHISampler::create(ID3D12Device* device, DX12DescriptorHeap& samplerHeap, const D3D12_SAMPLER_DESC& desc)
{
    if (!device) return;
    if (m_index == UINT32_MAX)
        m_index = samplerHeap.allocate(1);
    m_cpu = samplerHeap.cpuHandle(m_index);
    m_gpu = samplerHeap.gpuHandle(m_index);
    device->CreateSampler(&desc, m_cpu);
}

} // namespace Demon
