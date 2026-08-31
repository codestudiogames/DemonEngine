#include "DX12Swapchain.h"
#include "DX12Context.h"
#include "DX12DescriptorHeap.h"
#include "core/Logger.h"
#include <directx/d3dx12.h>

namespace Demon {

void DX12Swapchain::init(DX12Context& ctx, HWND hwnd, uint32_t width, uint32_t height,
                         DX12DescriptorHeap& rtvHeap, DX12DescriptorHeap& dsvHeap)
{
    m_ctx = &ctx;
    m_rtvHeap = &rtvHeap;
    m_dsvHeap = &dsvHeap;
    m_hwnd = hwnd;
    m_width = width;
    m_height = height;

    m_rtvBaseIndex = m_rtvHeap->allocate(k_frameCount);
    m_dsvIndex = m_dsvHeap->allocate(1);

    createSwapchain(hwnd, width, height);
    DEMON_ASSERT(createBackBuffers(), "Failed to create swapchain back buffers");
    DEMON_ASSERT(createDepthBuffer(width, height), "Failed to create depth buffer");
}

void DX12Swapchain::cleanup()
{
    if (m_ctx) {
        m_ctx->waitForGpu();
    }
    for (auto& b : m_backBuffers) b.Reset();
    m_depthBuffer.Reset();
    m_swapchain.Reset();
}

bool DX12Swapchain::resize(uint32_t width, uint32_t height)
{
    if (!m_swapchain) return false;
    if (width == 0 || height == 0) return false;
    if (width == m_width && height == m_height) return true;

    DEMON_LOG_INFO("DX12Swapchain: resize {}x{} -> {}x{}", m_width, m_height, width, height);

    const uint32_t oldWidth = m_width;
    const uint32_t oldHeight = m_height;
    m_width = width;
    m_height = height;

    if (m_ctx) {
        DEMON_LOG_INFO("DX12Swapchain: preparing context for resize.");
        m_ctx->prepareForResize();
        DEMON_LOG_INFO("DX12Swapchain: context prepared for resize.");
    }
    DEMON_LOG_INFO("DX12Swapchain: releasing old back buffers before ResizeBuffers.");
    for (auto& b : m_backBuffers) b.Reset();
    m_depthBuffer.Reset();

    DXGI_SWAP_CHAIN_DESC desc{};
    m_swapchain->GetDesc(&desc);
    DEMON_LOG_INFO("DX12Swapchain: calling ResizeBuffers for {}x{}.", width, height);
    HRESULT hr = m_swapchain->ResizeBuffers(k_frameCount, width, height, desc.BufferDesc.Format, desc.Flags);
    if (FAILED(hr)) {
        HRESULT removedReason = S_OK;
        if (m_ctx && m_ctx->getDevice())
            removedReason = m_ctx->getDevice()->GetDeviceRemovedReason();
        DEMON_LOG_WARN(
            "DX12Swapchain: ResizeBuffers failed for {}x{} (hr={:#010x}, device={:#010x}).",
            width, height, static_cast<uint32_t>(hr), static_cast<uint32_t>(removedReason));

        m_width = oldWidth;
        m_height = oldHeight;
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET ||
            removedReason == DXGI_ERROR_DEVICE_REMOVED || removedReason == DXGI_ERROR_DEVICE_RESET) {
            DEMON_LOG_ERROR("DX12Swapchain: resize aborted because the D3D12 device was removed.");
            return false;
        }

        if (!createBackBuffers() || !createDepthBuffer(oldWidth, oldHeight)) {
            DEMON_LOG_ERROR("DX12Swapchain: failed to restore swapchain resources after resize failure.");
            return false;
        }

        m_frameIndex = m_swapchain->GetCurrentBackBufferIndex();
        return false;
    }

    DEMON_LOG_INFO("DX12Swapchain: ResizeBuffers succeeded.");
    if (!createBackBuffers() || !createDepthBuffer(width, height)) {
        DEMON_LOG_ERROR("DX12Swapchain: failed to recreate render targets after resize.");
        return false;
    }
    m_frameIndex = m_swapchain->GetCurrentBackBufferIndex();
    return true;
}

void DX12Swapchain::present(bool vsync)
{
    UINT sync = vsync ? 1 : 0;
    const HRESULT hr = m_swapchain->Present(sync, 0);
    if (FAILED(hr)) {
        HRESULT removedReason = S_OK;
        if (m_ctx && m_ctx->getDevice())
            removedReason = m_ctx->getDevice()->GetDeviceRemovedReason();
        DEMON_LOG_ERROR("DX12Swapchain: Present failed (hr={:#010x}, device={:#010x}).",
                        static_cast<uint32_t>(hr), static_cast<uint32_t>(removedReason));
        return;
    }
    m_frameIndex = m_swapchain->GetCurrentBackBufferIndex();
}

void DX12Swapchain::createSwapchain(HWND hwnd, uint32_t width, uint32_t height)
{
    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.BufferCount = k_frameCount;
    sd.Width = width;
    sd.Height = height;
    sd.Format = m_backBufferFormat;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.SampleDesc.Count = 1;

    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapchain1;
    HRESULT hr = m_ctx->getFactory()->CreateSwapChainForHwnd(
        m_ctx->getCommandQueue(), hwnd, &sd, nullptr, nullptr, &swapchain1);
    DEMON_ASSERT(SUCCEEDED(hr), "Failed to create DXGI swapchain");

    m_ctx->getFactory()->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
    swapchain1.As(&m_swapchain);
    m_frameIndex = m_swapchain->GetCurrentBackBufferIndex();
}

bool DX12Swapchain::createBackBuffers()
{
    for (uint32_t i = 0; i < k_frameCount; ++i) {
        HRESULT hr = m_swapchain->GetBuffer(i, IID_PPV_ARGS(&m_backBuffers[i]));
        if (FAILED(hr)) {
            DEMON_LOG_ERROR("DX12Swapchain: failed to acquire back buffer {} (hr={:#010x})",
                            i, static_cast<uint32_t>(hr));
            return false;
        }
        const uint32_t idx = m_rtvBaseIndex + i;
        m_rtvHandles[i] = m_rtvHeap->cpuHandle(idx);
        m_ctx->getDevice()->CreateRenderTargetView(m_backBuffers[i].Get(), nullptr, m_rtvHandles[i]);
    }

    return true;
}

bool DX12Swapchain::createDepthBuffer(uint32_t width, uint32_t height)
{
    D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(
        m_depthFormat, width, height, 1, 0, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
    D3D12_CLEAR_VALUE clear{};
    clear.Format = m_depthFormat;
    clear.DepthStencil.Depth = 1.0f;
    clear.DepthStencil.Stencil = 0;

    auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    HRESULT hr = m_ctx->getDevice()->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &clear, IID_PPV_ARGS(&m_depthBuffer));
    if (FAILED(hr)) {
        HRESULT removedReason = S_OK;
        if (m_ctx && m_ctx->getDevice())
            removedReason = m_ctx->getDevice()->GetDeviceRemovedReason();
        DEMON_LOG_ERROR("DX12Swapchain: failed to create depth buffer {}x{} (hr={:#010x}, device={:#010x})",
                        width, height, static_cast<uint32_t>(hr), static_cast<uint32_t>(removedReason));
        return false;
    }

    m_dsvHandle = m_dsvHeap->cpuHandle(m_dsvIndex);
    D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
    dsv.Format = m_depthFormat;
    dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsv.Flags = D3D12_DSV_FLAG_NONE;
    m_ctx->getDevice()->CreateDepthStencilView(m_depthBuffer.Get(), &dsv, m_dsvHandle);
    return true;
}

} // namespace Demon
