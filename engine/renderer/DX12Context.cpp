#include "DX12Context.h"
#include "core/Logger.h"

namespace Demon {
namespace {

constexpr DWORD kFenceWaitTimeoutMs = 5000;

bool waitForFenceValue(ID3D12Fence* fence, HANDLE fenceEvent, uint64_t value, const char* label)
{
    if (!fence || !fenceEvent) {
        DEMON_LOG_ERROR("DX12Context: cannot wait for {} fence; fence or event is null.", label);
        return false;
    }

    const uint64_t completed = fence->GetCompletedValue();
    if (completed >= value)
        return true;

    const HRESULT hr = fence->SetEventOnCompletion(value, fenceEvent);
    if (FAILED(hr)) {
        DEMON_LOG_ERROR(
            "DX12Context: SetEventOnCompletion failed for {} fence (target={}, completed={}, hr={:#010x}).",
            label, value, completed, static_cast<uint32_t>(hr));
        return false;
    }

    const DWORD waitResult = WaitForSingleObject(fenceEvent, kFenceWaitTimeoutMs);
    if (waitResult == WAIT_OBJECT_0)
        return true;

    if (waitResult == WAIT_TIMEOUT) {
        DEMON_LOG_ERROR(
            "DX12Context: timeout waiting {} ms for {} fence (target={}, completed={}).",
            kFenceWaitTimeoutMs, label, value, fence->GetCompletedValue());
    } else {
        DEMON_LOG_ERROR(
            "DX12Context: WaitForSingleObject failed for {} fence (result={}, target={}, completed={}).",
            label, static_cast<uint32_t>(waitResult), value, fence->GetCompletedValue());
    }

    return false;
}

} // namespace

void DX12Context::init(bool enableDebug)
{
    createDevice(enableDebug);

    D3D12_COMMAND_QUEUE_DESC qd{};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    qd.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    DEMON_ASSERT(SUCCEEDED(m_device->CreateCommandQueue(&qd, IID_PPV_ARGS(&m_commandQueue))),
                 "Failed to create D3D12 command queue");

    for (uint32_t i = 0; i < k_frameCount; ++i) {
        DEMON_ASSERT(SUCCEEDED(m_device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocators[i]))),
            "Failed to create command allocator");
        m_fenceValues[i] = 0;
    }

    DEMON_ASSERT(SUCCEEDED(m_device->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocators[0].Get(), nullptr,
        IID_PPV_ARGS(&m_commandList))), "Failed to create command list");
    m_commandList->Close();

    DEMON_ASSERT(SUCCEEDED(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence))),
                 "Failed to create fence");
    m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    DEMON_ASSERT(m_fenceEvent != nullptr, "Failed to create fence event");

    DEMON_LOG_INFO("DX12Context: device and command queue ready.");
}

void DX12Context::shutdown()
{
    waitForGpu();
    if (m_fenceEvent) {
        CloseHandle(m_fenceEvent);
        m_fenceEvent = nullptr;
    }
    m_commandList.Reset();
    for (auto& a : m_commandAllocators) a.Reset();
    m_commandQueue.Reset();
    m_fence.Reset();
    m_device.Reset();
    m_factory.Reset();
    m_fenceValues.fill(0);
    m_immediateFenceValue = 0;
    m_nextFenceValue = 1;
}

void DX12Context::createDevice(bool enableDebug)
{
    UINT flags = 0;
#if defined(DEMON_DX12_DEBUG)
    if (enableDebug) {
        Microsoft::WRL::ComPtr<ID3D12Debug> debug;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
            debug->EnableDebugLayer();
            flags |= DXGI_CREATE_FACTORY_DEBUG;
            DEMON_LOG_INFO("DX12Context: debug layer enabled.");
        }
    }
#else
    (void)enableDebug;
#endif

    DEMON_ASSERT(SUCCEEDED(CreateDXGIFactory2(flags, IID_PPV_ARGS(&m_factory))),
                 "Failed to create DXGI factory");

    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; m_factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 desc{};
        adapter->GetDesc1(&desc);
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
        if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device))))
            break;
    }

    if (!m_device) {
        DEMON_ASSERT(SUCCEEDED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device))),
                     "Failed to create D3D12 device");
    }
}

void DX12Context::beginFrame(uint32_t frameIndex)
{
    DEMON_ASSERT(SUCCEEDED(m_commandAllocators[frameIndex]->Reset()), "Failed to reset command allocator");
    HRESULT hr = m_commandList->Reset(m_commandAllocators[frameIndex].Get(), nullptr);
    if (FAILED(hr)) {
        HRESULT closeHr = m_commandList->Close();
        DEMON_LOG_WARN("DX12Context: command list reset failed for frame {} (hr={:#010x}, close={:#010x}); retrying.",
                       frameIndex, static_cast<uint32_t>(hr), static_cast<uint32_t>(closeHr));
        hr = m_commandList->Reset(m_commandAllocators[frameIndex].Get(), nullptr);
    }
    DEMON_ASSERT(SUCCEEDED(hr), "Failed to reset command list");
}

void DX12Context::endFrame()
{
    DEMON_ASSERT(SUCCEEDED(m_commandList->Close()), "Failed to close command list");
}

void DX12Context::discardFrame(uint32_t frameIndex)
{
    DEMON_ASSERT(SUCCEEDED(m_commandAllocators[frameIndex]->Reset()), "Failed to reset command allocator");
    HRESULT hr = m_commandList->Reset(m_commandAllocators[frameIndex].Get(), nullptr);
    if (FAILED(hr)) {
        HRESULT closeHr = m_commandList->Close();
        DEMON_LOG_WARN("DX12Context: discard reset failed for frame {} (hr={:#010x}, close={:#010x}); retrying.",
                       frameIndex, static_cast<uint32_t>(hr), static_cast<uint32_t>(closeHr));
        hr = m_commandList->Reset(m_commandAllocators[frameIndex].Get(), nullptr);
    }
    DEMON_ASSERT(SUCCEEDED(hr), "Failed to reset command list");
    DEMON_ASSERT(SUCCEEDED(m_commandList->Close()), "Failed to close discarded command list");
}

void DX12Context::waitForFrame(uint32_t frameIndex)
{
    const uint64_t fenceValue = m_fenceValues[frameIndex];
    if (fenceValue == 0) return;
    (void)waitForFenceValue(m_fence.Get(), m_fenceEvent, fenceValue, "frame");
}

void DX12Context::waitForGpu()
{
    // Wait for the latest fence value across frame submits and immediate work.
    uint64_t value = m_immediateFenceValue;
    for (uint32_t i = 0; i < k_frameCount; ++i)
        value = std::max(value, m_fenceValues[i]);
    if (value == 0) return;
    (void)waitForFenceValue(m_fence.Get(), m_fenceEvent, value, "gpu");
}

void DX12Context::prepareForResize()
{
    if (!m_commandQueue || !m_fence || !m_commandList) {
        DEMON_LOG_WARN("DX12Context: prepareForResize skipped because queue, fence, or command list is unavailable.");
        return;
    }

    DEMON_LOG_INFO("DX12Context: prepareForResize begin.");

    const uint64_t fenceValue = m_nextFenceValue++;
    const HRESULT signalHr = m_commandQueue->Signal(m_fence.Get(), fenceValue);
    if (FAILED(signalHr)) {
        DEMON_LOG_ERROR("DX12Context: failed to signal resize fence {} (hr={:#010x}).",
                        fenceValue, static_cast<uint32_t>(signalHr));
        return;
    }

    if (!waitForFenceValue(m_fence.Get(), m_fenceEvent, fenceValue, "resize")) {
        DEMON_LOG_WARN("DX12Context: prepareForResize continuing after fence wait issue.");
    }

    for (uint32_t i = 0; i < k_frameCount; ++i) {
        const HRESULT hr = m_commandAllocators[i]->Reset();
        if (FAILED(hr)) {
            DEMON_LOG_WARN("DX12Context: command allocator reset failed during prepareForResize for frame {} (hr={:#010x}).",
                           i, static_cast<uint32_t>(hr));
        }
    }

    HRESULT hr = m_commandList->Reset(m_commandAllocators[0].Get(), nullptr);
    if (FAILED(hr)) {
        const HRESULT closeHr = m_commandList->Close();
        DEMON_LOG_WARN("DX12Context: command list reset failed during prepareForResize (hr={:#010x}, close={:#010x}); retrying.",
                       static_cast<uint32_t>(hr), static_cast<uint32_t>(closeHr));
        hr = m_commandList->Reset(m_commandAllocators[0].Get(), nullptr);
    }

    if (SUCCEEDED(hr)) {
        const HRESULT closeHr = m_commandList->Close();
        if (FAILED(closeHr)) {
            DEMON_LOG_WARN("DX12Context: command list close failed during prepareForResize (hr={:#010x}).",
                           static_cast<uint32_t>(closeHr));
        }
    } else {
        DEMON_LOG_WARN("DX12Context: prepareForResize could not reset command list cleanly (hr={:#010x}).",
                       static_cast<uint32_t>(hr));
    }

    m_fenceValues.fill(0);
    m_immediateFenceValue = 0;
    DEMON_LOG_INFO("DX12Context: prepareForResize complete.");
}

void DX12Context::signalFence(uint32_t frameIndex)
{
    const uint64_t fenceValue = m_nextFenceValue++;
    const HRESULT hr = m_commandQueue->Signal(m_fence.Get(), fenceValue);
    if (FAILED(hr)) {
        DEMON_LOG_ERROR("DX12Context: failed to signal frame fence {} (hr={:#010x}).",
                        fenceValue, static_cast<uint32_t>(hr));
        return;
    }
    m_fenceValues[frameIndex] = fenceValue;
}

void DX12Context::immediateSubmit(const std::function<void(ID3D12GraphicsCommandList*)>& fn)
{
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> alloc;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> list;

    DEMON_ASSERT(SUCCEEDED(m_device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc))),
        "Failed to create upload command allocator");
    DEMON_ASSERT(SUCCEEDED(m_device->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(), nullptr, IID_PPV_ARGS(&list))),
        "Failed to create upload command list");

    fn(list.Get());
    DEMON_ASSERT(SUCCEEDED(list->Close()), "Failed to close upload command list");
    ID3D12CommandList* lists[] = { list.Get() };
    m_commandQueue->ExecuteCommandLists(1, lists);

    const uint64_t fenceValue = m_nextFenceValue++;
    m_immediateFenceValue = fenceValue;
    DEMON_ASSERT(SUCCEEDED(m_commandQueue->Signal(m_fence.Get(), fenceValue)),
                 "Failed to signal upload fence");
    (void)waitForFenceValue(m_fence.Get(), m_fenceEvent, fenceValue, "upload");
}

} // namespace Demon
