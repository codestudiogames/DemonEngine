#include "Texture.h"
#include "DX12Context.h"
#include "DX12DescriptorHeap.h"
#include "renderer/stb_image.h"
#include "core/Logger.h"
#include <directx/d3dx12.h>

namespace Demon {

static DXGI_FORMAT pickFormat(bool srgb)
{
    return srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
}

std::shared_ptr<Texture> Texture::create(const TextureSpec& spec, DX12Context& ctx, DX12DescriptorHeap& srvHeap)
{
    auto tex = std::make_shared<Texture>();
    tex->m_spec = spec;
    tex->createFromPixels(nullptr, spec.width, spec.height, ctx, srvHeap, spec.srgb);
    return tex;
}

std::shared_ptr<Texture> Texture::loadFromFile(const std::string& path, DX12Context& ctx, DX12DescriptorHeap& srvHeap, bool srgb)
{
    // Guard: reject empty or non-existent paths before touching stb/GPU
    if (path.empty()) {
        DEMON_LOG_WARN("Texture::loadFromFile: empty path supplied, skipping.");
        return nullptr;
    }
    if (!std::filesystem::exists(path)) {
        DEMON_LOG_ERROR("Texture: failed to load '{}' (file not found)", path);
        return nullptr;
    }

    auto img = stb_load_rgba(path.c_str());
    if (!img.pixels || img.w <= 0 || img.h <= 0) {
        DEMON_LOG_ERROR("Texture: failed to load '{}'", path);
        return nullptr;
    }

    auto tex = std::make_shared<Texture>();
    tex->m_path = path;
    tex->createFromPixels(img.pixels, static_cast<uint32_t>(img.w), static_cast<uint32_t>(img.h), ctx, srvHeap, srgb);
    stb_free(img.pixels);
    return tex;
}

std::shared_ptr<Texture> Texture::createWhite1x1(DX12Context& ctx, DX12DescriptorHeap& srvHeap)
{
    const uint8_t white[4] = { 255, 255, 255, 255 };
    return createFromRGBA(white, 1, 1, ctx, srvHeap, false);
}

std::shared_ptr<Texture> Texture::createBlack1x1(DX12Context& ctx, DX12DescriptorHeap& srvHeap)
{
    const uint8_t black[4] = { 0, 0, 0, 255 };
    return createFromRGBA(black, 1, 1, ctx, srvHeap, false);
}

std::shared_ptr<Texture> Texture::createNormal1x1(DX12Context& ctx, DX12DescriptorHeap& srvHeap)
{
    const uint8_t normal[4] = { 128, 128, 255, 255 };
    return createFromRGBA(normal, 1, 1, ctx, srvHeap, false);
}

std::shared_ptr<Texture> Texture::createFromRGBA(const uint8_t* pixels, uint32_t width, uint32_t height,
                                                  DX12Context& ctx, DX12DescriptorHeap& srvHeap, bool srgb)
{
    auto tex = std::make_shared<Texture>();
    tex->createFromPixels(pixels, width, height, ctx, srvHeap, srgb);
    return tex;
}

void Texture::createFromPixels(const uint8_t* pixels, uint32_t width, uint32_t height,
                               DX12Context& ctx, DX12DescriptorHeap& srvHeap, bool srgb)
{
    m_spec.width = width;
    m_spec.height = height;
    m_spec.format = pickFormat(srgb);
    m_spec.srgb = srgb;

    auto device = ctx.getDevice();
    D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(
        m_spec.format, width, height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_NONE);

    auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    HRESULT hr = device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_texture));
    DEMON_ASSERT(SUCCEEDED(hr), "Failed to create texture resource");

    if (pixels) {
        const uint64_t uploadSize = GetRequiredIntermediateSize(m_texture.Get(), 0, 1);
        auto uploadProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);
        Microsoft::WRL::ComPtr<ID3D12Resource> upload;
        hr = device->CreateCommittedResource(
            &uploadProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload));
        DEMON_ASSERT(SUCCEEDED(hr), "Failed to create texture upload buffer");

        D3D12_SUBRESOURCE_DATA sub{};
        sub.pData = pixels;
        sub.RowPitch = static_cast<LONG_PTR>(width) * 4;
        sub.SlicePitch = sub.RowPitch * height;

        ctx.immediateSubmit([&](ID3D12GraphicsCommandList* cmd) {
            UpdateSubresources(cmd, m_texture.Get(), upload.Get(), 0, 0, 1, &sub);
            auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                m_texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            cmd->ResourceBarrier(1, &barrier);
        });
    } else {
        ctx.immediateSubmit([&](ID3D12GraphicsCommandList* cmd) {
            auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                m_texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            cmd->ResourceBarrier(1, &barrier);
        });
    }

    createSrv(device, srvHeap, srgb);
}

void Texture::createSrv(ID3D12Device* device, DX12DescriptorHeap& srvHeap, bool srgb)
{
    m_srvIndex = srvHeap.allocate(1);
    m_srvCpu = srvHeap.cpuHandle(m_srvIndex);
    m_srvGpu = srvHeap.gpuHandle(m_srvIndex);

    D3D12_SHADER_RESOURCE_VIEW_DESC sv{};
    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sv.Format = pickFormat(srgb);
    sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sv.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(m_texture.Get(), &sv, m_srvCpu);
}

void Texture::destroy()
{
    m_texture.Reset();
    m_srvIndex = UINT32_MAX;
    m_srvCpu = {};
    m_srvGpu = {};
}

} // namespace Demon
