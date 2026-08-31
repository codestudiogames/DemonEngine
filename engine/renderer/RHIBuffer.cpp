// ============================================================================
//  DemonEngine::RHIBuffer  -  DX12-backed buffer implementation
// ============================================================================
#include "RHIBuffer.h"
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

void RHIBuffer::create(ID3D12Device* device, const RHIBufferDesc& desc, const char* debugName)
{
    destroy();
    if (!device || desc.size == 0) {
        DEMON_LOG_WARN("RHIBuffer: create called with invalid device or size.");
        return;
    }

    m_size = desc.size;
    m_state = desc.initialState;
    m_cpuVisible = (desc.heapType == D3D12_HEAP_TYPE_UPLOAD) || (desc.heapType == D3D12_HEAP_TYPE_READBACK);

    auto heapProps = CD3DX12_HEAP_PROPERTIES(desc.heapType);
    auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(desc.size, desc.flags);

    HRESULT hr = device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        desc.initialState,
        nullptr,
        IID_PPV_ARGS(&m_resource));
    DEMON_ASSERT(SUCCEEDED(hr), "Failed to create RHIBuffer");

    if (m_resource && debugName) {
        auto wide = toWide(debugName);
        if (!wide.empty())
            m_resource->SetName(wide.c_str());
    }
}

void RHIBuffer::destroy()
{
    if (m_mapped && m_resource) {
        m_resource->Unmap(0, nullptr);
    }
    m_mapped = false;
    m_mappedPtr = nullptr;
    m_resource.Reset();
    m_size = 0;
    m_state = D3D12_RESOURCE_STATE_COMMON;
    m_cpuVisible = false;
}

void RHIBuffer::transition(ID3D12GraphicsCommandList* cmd, D3D12_RESOURCE_STATES newState)
{
    if (!cmd || !m_resource || m_state == newState) return;
    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(m_resource.Get(), m_state, newState);
    cmd->ResourceBarrier(1, &barrier);
    m_state = newState;
}

void* RHIBuffer::map()
{
    if (!m_cpuVisible || !m_resource)
        return nullptr;
    if (!m_mapped) {
        CD3DX12_RANGE range(0, 0);
        HRESULT hr = m_resource->Map(0, &range, &m_mappedPtr);
        DEMON_ASSERT(SUCCEEDED(hr), "RHIBuffer: Map failed");
        m_mapped = true;
    }
    return m_mappedPtr;
}

void RHIBuffer::unmap()
{
    if (!m_resource || !m_mapped) return;
    m_resource->Unmap(0, nullptr);
    m_mapped = false;
    m_mappedPtr = nullptr;
}

} // namespace Demon
