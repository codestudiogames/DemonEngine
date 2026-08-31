#pragma once
// ============================================================================
//  DemonEngine::RHIBuffer
//  Minimal render-hardware abstraction for GPU buffers (DX12-backed).
// ============================================================================
#include "core/DemonPCH.h"

namespace Demon {

enum class RHIBufferUsage : uint8_t {
    Generic,
    Vertex,
    Index,
    Constant,
    Upload,
    Readback
};

struct RHIBufferDesc {
    uint64_t size = 0;
    D3D12_HEAP_TYPE heapType = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
    D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON;
    RHIBufferUsage usage = RHIBufferUsage::Generic;
};

class RHIBuffer {
public:
    RHIBuffer() = default;
    ~RHIBuffer() { destroy(); }

    // Main-thread-only: create GPU buffer resource.
    void create(ID3D12Device* device, const RHIBufferDesc& desc, const char* debugName = nullptr);
    // Main-thread-only: release resource.
    void destroy();

    // Render-thread-safe: record a transition barrier if needed.
    void transition(ID3D12GraphicsCommandList* cmd, D3D12_RESOURCE_STATES newState);

    // Job-thread-safe for upload buffers only.
    void* map();
    void unmap();

    [[nodiscard]] ID3D12Resource* getResource() const { return m_resource.Get(); }
    [[nodiscard]] uint64_t getSize() const { return m_size; }
    [[nodiscard]] D3D12_RESOURCE_STATES getState() const { return m_state; }
    [[nodiscard]] bool isValid() const { return m_resource != nullptr; }

private:
    Microsoft::WRL::ComPtr<ID3D12Resource> m_resource;
    uint64_t m_size = 0;
    D3D12_RESOURCE_STATES m_state = D3D12_RESOURCE_STATE_COMMON;
    bool m_cpuVisible = false;
    bool m_mapped = false;
    void* m_mappedPtr = nullptr;
};

} // namespace Demon
