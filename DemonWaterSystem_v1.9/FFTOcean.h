#pragma once
#include "Core/DemonCore.h"
#include "Core/Math/DemonMath.h"
#include "RHI/DX12/DX12Texture.h"
#include "RHI/DX12/DX12Buffer.h"
#include "WaterComponent.h"

namespace Demon
{
    class CommandList;

    // -------------------------------------------------------------------------
    // FFT sizes we support
    // -------------------------------------------------------------------------
    enum class FFTSize : int { N128 = 128, N256 = 256, N512 = 512 };

    // -------------------------------------------------------------------------
    // GPU constant buffer for FFT compute shaders
    // -------------------------------------------------------------------------
    struct FFTOceanCB
    {
        float  Time;
        float  DeltaTime;
        float  PatchSize;           // world metres
        float  WindSpeed;
        float  WindDirX;
        float  WindDirY;
        float  Fetch;
        float  Choppiness;
        float  WaveHeightScale;
        int    N;                   // FFT resolution (power of 2)
        int    Log2N;
        float  Gravity;             // 9.81
        float  Padding[4];
    };

    // -------------------------------------------------------------------------
    // FFTOcean
    //   Implements GPU Cooley-Tukey FFT ocean simulation.
    //
    //   Pipeline:
    //     1. InitSpectrum  – one-time Phillips H0 spectrum setup
    //     2. UpdateSpectrum – per-frame animate H(k,t) from H0
    //     3. FFT (row)     – horizontal 1D FFT on displacement/slope textures
    //     4. FFT (col)     – vertical  1D FFT
    //     5. Permute+Pack  – assemble displacement XYZ + Jacobian into R16G16B16A16
    //     6. NormalGen     – sobel on displacement → world-space normal map
    // -------------------------------------------------------------------------
    class FFTOcean
    {
    public:
        FFTOcean();
        ~FFTOcean();

        bool Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* initCmd,
                        FFTSize size, float patchSize);
        void Shutdown();

        // Drive full simulation (call once per frame, before water render)
        void Simulate(CommandList& cmd, const WaterSettings& settings, float time);

        // Accessors for render passes
        ID3D12Resource* GetDisplacementMap() const { return m_DisplacementMap.Get(); }
        ID3D12Resource* GetNormalMap()       const { return m_NormalMap.Get(); }
        ID3D12Resource* GetFoamMap()         const { return m_FoamMap.Get(); }   // R = Jacobian

        D3D12_GPU_DESCRIPTOR_HANDLE GetDisplacementSRV() const { return m_DisplacementSRV; }
        D3D12_GPU_DESCRIPTOR_HANDLE GetNormalSRV()       const { return m_NormalSRV; }
        D3D12_GPU_DESCRIPTOR_HANDLE GetFoamSRV()         const { return m_FoamSRV; }

        int   GetResolution() const { return m_N; }
        float GetPatchSize()  const { return m_PatchSize; }

    private:
        // PSO helpers
        bool CreatePSOs(ID3D12Device* device);
        bool CreateResources(ID3D12Device* device, ID3D12GraphicsCommandList* initCmd);
        bool CreateDescriptors(ID3D12Device* device);
        void BakeButterfltyTexture(ID3D12Device* device, ID3D12GraphicsCommandList* cmd);

        // Sub-passes
        void PassInitSpectrum(CommandList& cmd, const WaterSettings& s, float time);
        void PassUpdateSpectrum(CommandList& cmd, float time);
        void PassFFT(CommandList& cmd, bool horizontal, ID3D12Resource* src, ID3D12Resource* dst);
        void PassPermuteAndPack(CommandList& cmd);
        void PassGenerateNormals(CommandList& cmd);

        // State
        int   m_N         = 512;
        float m_PatchSize = 400.f;
        bool  m_SpectrumDirty = true;  // regen when wind changes

        // Textures (R32G32_FLOAT complex)
        ComPtr<ID3D12Resource> m_H0Texture;           // initial spectrum H0(k)
        ComPtr<ID3D12Resource> m_HtTextureDx;         // H(k,t) displacement X
        ComPtr<ID3D12Resource> m_HtTextureDy;         // H(k,t) displacement Y
        ComPtr<ID3D12Resource> m_HtTextureDz;         // H(k,t) displacement Z
        ComPtr<ID3D12Resource> m_HtTextureSlopeX;     // slope for normals
        ComPtr<ID3D12Resource> m_HtTextureSlopeZ;
        ComPtr<ID3D12Resource> m_PingPong[2];          // FFT butterfly temp

        // Output textures (read by pixel shader)
        ComPtr<ID3D12Resource> m_DisplacementMap;     // R16G16B16A16: XYZ disp, W=unused
        ComPtr<ID3D12Resource> m_NormalMap;           // R16G16B16A16: XYZ world normal
        ComPtr<ID3D12Resource> m_FoamMap;             // R16_FLOAT:    Jacobian (foam mask)

        // Butterfly lookup texture (log2(N) stages x N x 2)
        ComPtr<ID3D12Resource> m_ButterflyTexture;

        // Constant buffer
        ComPtr<ID3D12Resource> m_CB;
        FFTOceanCB*            m_CBData = nullptr;

        // Descriptor heap (SRV/UAV)
        ComPtr<ID3D12DescriptorHeap> m_DescHeap;
        UINT m_DescSize = 0;

        // SRV handles for pixel shader
        D3D12_GPU_DESCRIPTOR_HANDLE m_DisplacementSRV = {};
        D3D12_GPU_DESCRIPTOR_HANDLE m_NormalSRV       = {};
        D3D12_GPU_DESCRIPTOR_HANDLE m_FoamSRV         = {};

        // PSOs (compute)
        ComPtr<ID3D12PipelineState>      m_PSO_InitSpectrum;
        ComPtr<ID3D12PipelineState>      m_PSO_UpdateSpectrum;
        ComPtr<ID3D12PipelineState>      m_PSO_FFTHorizontal;
        ComPtr<ID3D12PipelineState>      m_PSO_FFTVertical;
        ComPtr<ID3D12PipelineState>      m_PSO_PermutePack;
        ComPtr<ID3D12PipelineState>      m_PSO_NormalGen;
        ComPtr<ID3D12RootSignature>      m_RS;
    };

} // namespace Demon
