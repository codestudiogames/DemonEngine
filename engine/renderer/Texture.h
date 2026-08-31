#pragma once
// ==============================================================================
//  DemonEngine::Texture
//  2D texture loaded from disk (stb_image) and uploaded to GPU (DX12).
// ==============================================================================
#include "core/DemonPCH.h"

namespace Demon {

class DX12Context;
class DX12DescriptorHeap;

enum class TextureFilter { Nearest, Linear };
enum class TextureWrap   { Repeat, ClampToEdge, MirroredRepeat };

struct TextureSpec {
    uint32_t   width   = 1;
    uint32_t   height  = 1;
    DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
    bool       srgb   = true;
};

class Texture {
public:
    Texture() = default;
    ~Texture() { destroy(); }

    static std::shared_ptr<Texture> create(const TextureSpec& spec, DX12Context& ctx, DX12DescriptorHeap& srvHeap);
    static std::shared_ptr<Texture> loadFromFile(const std::string& path, DX12Context& ctx, DX12DescriptorHeap& srvHeap, bool srgb = true);
    static std::shared_ptr<Texture> createWhite1x1 (DX12Context& ctx, DX12DescriptorHeap& srvHeap);
    static std::shared_ptr<Texture> createBlack1x1 (DX12Context& ctx, DX12DescriptorHeap& srvHeap);
    static std::shared_ptr<Texture> createNormal1x1(DX12Context& ctx, DX12DescriptorHeap& srvHeap);
    static std::shared_ptr<Texture> createFromRGBA(const uint8_t* pixels, uint32_t width, uint32_t height,
                                                   DX12Context& ctx, DX12DescriptorHeap& srvHeap, bool srgb = true);

    [[nodiscard]] ID3D12Resource* getResource() const { return m_texture.Get(); }
    [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE getSrvGpuHandle() const { return m_srvGpu; }
    [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE getSrvCpuHandle() const { return m_srvCpu; }
    [[nodiscard]] uint32_t getWidth() const { return m_spec.width; }
    [[nodiscard]] uint32_t getHeight() const { return m_spec.height; }
    [[nodiscard]] const std::string& getPath() const { return m_path; }
    [[nodiscard]] bool isLoaded() const { return m_texture != nullptr; }

    void destroy();

private:
    void createFromPixels(const uint8_t* pixels, uint32_t width, uint32_t height,
                          DX12Context& ctx, DX12DescriptorHeap& srvHeap, bool srgb);
    void createSrv(ID3D12Device* device, DX12DescriptorHeap& srvHeap, bool srgb);

    TextureSpec m_spec{};
    std::string m_path;

    Microsoft::WRL::ComPtr<ID3D12Resource> m_texture;
    D3D12_CPU_DESCRIPTOR_HANDLE m_srvCpu{};
    D3D12_GPU_DESCRIPTOR_HANDLE m_srvGpu{};
    uint32_t m_srvIndex = UINT32_MAX;
};

} // namespace Demon
