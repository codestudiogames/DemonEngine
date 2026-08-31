#pragma once
// ==============================================================================
//  DemonEngine::DX12Swapchain
// ==============================================================================
#include "core/DemonPCH.h"

namespace Demon {

class DX12Context;
class DX12DescriptorHeap;

class DX12Swapchain {
public:
    static constexpr uint32_t k_frameCount = 2;

    void init(DX12Context& ctx, HWND hwnd, uint32_t width, uint32_t height,
              DX12DescriptorHeap& rtvHeap, DX12DescriptorHeap& dsvHeap);
    void cleanup();
    bool resize(uint32_t width, uint32_t height);

    void present(bool vsync);
    uint32_t getCurrentFrameIndex() const { return m_frameIndex; }

    [[nodiscard]] DXGI_FORMAT getBackBufferFormat() const { return m_backBufferFormat; }
    [[nodiscard]] DXGI_FORMAT getDepthFormat() const { return m_depthFormat; }
    [[nodiscard]] uint32_t getWidth() const { return m_width; }
    [[nodiscard]] uint32_t getHeight() const { return m_height; }
    [[nodiscard]] ID3D12Resource* getBackBuffer(uint32_t i) const { return m_backBuffers[i].Get(); }
    [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE getRTV(uint32_t i) const { return m_rtvHandles[i]; }
    [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE getDSV() const { return m_dsvHandle; }

private:
    void createSwapchain(HWND hwnd, uint32_t width, uint32_t height);
    bool createBackBuffers();
    bool createDepthBuffer(uint32_t width, uint32_t height);

    DX12Context* m_ctx = nullptr;
    DX12DescriptorHeap* m_rtvHeap = nullptr;
    DX12DescriptorHeap* m_dsvHeap = nullptr;
    HWND m_hwnd = nullptr;

    Microsoft::WRL::ComPtr<IDXGISwapChain3> m_swapchain;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, k_frameCount> m_backBuffers{};
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, k_frameCount> m_rtvHandles{};
    Microsoft::WRL::ComPtr<ID3D12Resource> m_depthBuffer;
    D3D12_CPU_DESCRIPTOR_HANDLE m_dsvHandle{};

    DXGI_FORMAT m_backBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    DXGI_FORMAT m_depthFormat = DXGI_FORMAT_D32_FLOAT;
    uint32_t m_frameIndex = 0;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    uint32_t m_rtvBaseIndex = 0;
    uint32_t m_dsvIndex = 0;
};

} // namespace Demon
