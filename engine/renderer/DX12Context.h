#pragma once
// ==============================================================================
//  DemonEngine::DX12Context
//  Owns device, queue, command list, and fence for DirectX 12.
// ==============================================================================
#include "core/DemonPCH.h"

namespace Demon {

class DX12Context {
public:
    static constexpr uint32_t k_frameCount = 2;

    void init(bool enableDebug);
    void shutdown();

    void beginFrame(uint32_t frameIndex);
    void endFrame();
    void discardFrame(uint32_t frameIndex);
    void waitForFrame(uint32_t frameIndex);
    void waitForGpu();
    void prepareForResize();

    // Synchronous one-shot submit for uploads.
    void immediateSubmit(const std::function<void(ID3D12GraphicsCommandList*)>& fn);

    [[nodiscard]] ID3D12Device* getDevice() const { return m_device.Get(); }
    [[nodiscard]] IDXGIFactory4* getFactory() const { return m_factory.Get(); }
    [[nodiscard]] ID3D12CommandQueue* getCommandQueue() const { return m_commandQueue.Get(); }
    [[nodiscard]] ID3D12GraphicsCommandList* getCommandList() const { return m_commandList.Get(); }
    [[nodiscard]] ID3D12Fence* getFence() const { return m_fence.Get(); }
    [[nodiscard]] HANDLE getFenceEvent() const { return m_fenceEvent; }
    [[nodiscard]] uint64_t getFenceValue(uint32_t frameIndex) const { return m_fenceValues[frameIndex]; }
    void signalFence(uint32_t frameIndex);

private:
    void createDevice(bool enableDebug);

    Microsoft::WRL::ComPtr<IDXGIFactory4> m_factory;
    Microsoft::WRL::ComPtr<ID3D12Device> m_device;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_commandQueue;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_commandList;
    std::array<Microsoft::WRL::ComPtr<ID3D12CommandAllocator>, k_frameCount> m_commandAllocators{};

    Microsoft::WRL::ComPtr<ID3D12Fence> m_fence;
    std::array<uint64_t, k_frameCount> m_fenceValues{};
    uint64_t m_immediateFenceValue = 0;
    uint64_t m_nextFenceValue = 1;
    HANDLE m_fenceEvent = nullptr;
};

} // namespace Demon
