#include "FFTOcean.h"
#include "RHI/DX12/DX12CommandList.h"
#include "RHI/DX12/DX12Helpers.h"
#include "Core/DemonLog.h"
#include <random>
#include <cmath>

namespace Demon
{
    // -------------------------------------------------------------------------
    // Helpers
    // -------------------------------------------------------------------------
    static int Log2i(int n)
    {
        int r = 0;
        while (n >>= 1) ++r;
        return r;
    }

    // -------------------------------------------------------------------------
    FFTOcean::FFTOcean()  = default;
    FFTOcean::~FFTOcean() { Shutdown(); }

    // -------------------------------------------------------------------------
    bool FFTOcean::Initialize(ID3D12Device*               device,
                              ID3D12GraphicsCommandList*  initCmd,
                              FFTSize                     size,
                              float                       patchSize)
    {
        m_N         = static_cast<int>(size);
        m_PatchSize = patchSize;

        DEMON_LOG_INFO("FFTOcean: Initializing N={} patchSize={}m", m_N, m_PatchSize);

        if (!CreateResources(device, initCmd)) return false;
        if (!CreateDescriptors(device))        return false;
        if (!CreatePSOs(device))               return false;

        BakeButterfltyTexture(device, initCmd);

        m_SpectrumDirty = true;
        DEMON_LOG_INFO("FFTOcean: Ready.");
        return true;
    }

    // -------------------------------------------------------------------------
    void FFTOcean::Shutdown()
    {
        m_H0Texture.Reset();
        m_HtTextureDx.Reset();
        m_HtTextureDy.Reset();
        m_HtTextureDz.Reset();
        m_HtTextureSlopeX.Reset();
        m_HtTextureSlopeZ.Reset();
        m_PingPong[0].Reset();
        m_PingPong[1].Reset();
        m_DisplacementMap.Reset();
        m_NormalMap.Reset();
        m_FoamMap.Reset();
        m_ButterflyTexture.Reset();
        m_CB.Reset();
        m_DescHeap.Reset();
        m_PSO_InitSpectrum.Reset();
        m_PSO_UpdateSpectrum.Reset();
        m_PSO_FFTHorizontal.Reset();
        m_PSO_FFTVertical.Reset();
        m_PSO_PermutePack.Reset();
        m_PSO_NormalGen.Reset();
        m_RS.Reset();
    }

    // -------------------------------------------------------------------------
    void FFTOcean::Simulate(CommandList& cmd, const WaterSettings& s, float time)
    {
        // Update CB
        m_CBData->Time            = time;
        m_CBData->PatchSize       = m_PatchSize;
        m_CBData->WindSpeed       = s.WindSpeed;
        float radDir              = s.WindDirection * (3.14159265f / 180.f);
        m_CBData->WindDirX        = std::cos(radDir);
        m_CBData->WindDirY        = std::sin(radDir);
        m_CBData->Fetch           = s.Fetch;
        m_CBData->Choppiness      = s.Choppiness;
        m_CBData->WaveHeightScale = s.WaveHeight;
        m_CBData->N               = m_N;
        m_CBData->Log2N           = Log2i(m_N);
        m_CBData->Gravity         = 9.81f;
        m_CBData->TimeScale       = s.TimeScale;

        auto* cl = cmd.GetCommandList();
        cl->SetComputeRootSignature(m_RS.Get());

        // 1. Init spectrum (only if wind changed)
        if (m_SpectrumDirty)
        {
            PassInitSpectrum(cmd, s, time);
            m_SpectrumDirty = false;
        }

        // 2. Animate spectrum
        PassUpdateSpectrum(cmd, time);

        // 3. FFT rows on Dx, Dy, Dz, Sx, Sz
        PassFFT(cmd, true,  m_HtTextureDx.Get(), m_PingPong[0].Get());
        PassFFT(cmd, false, m_PingPong[0].Get(), m_HtTextureDx.Get());

        PassFFT(cmd, true,  m_HtTextureDy.Get(), m_PingPong[0].Get());
        PassFFT(cmd, false, m_PingPong[0].Get(), m_HtTextureDy.Get());

        PassFFT(cmd, true,  m_HtTextureDz.Get(), m_PingPong[0].Get());
        PassFFT(cmd, false, m_PingPong[0].Get(), m_HtTextureDz.Get());

        PassFFT(cmd, true,  m_HtTextureSlopeX.Get(), m_PingPong[0].Get());
        PassFFT(cmd, false, m_PingPong[0].Get(),      m_HtTextureSlopeX.Get());

        PassFFT(cmd, true,  m_HtTextureSlopeZ.Get(), m_PingPong[0].Get());
        PassFFT(cmd, false, m_PingPong[0].Get(),      m_HtTextureSlopeZ.Get());

        // 4. Pack displacement + compute Jacobian
        PassPermuteAndPack(cmd);

        // 5. Generate world-space normals via Sobel
        PassGenerateNormals(cmd);

        // Transition output textures to SRV for pixel shader
        DX12Helpers::TransitionResource(cl, m_DisplacementMap.Get(),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        DX12Helpers::TransitionResource(cl, m_NormalMap.Get(),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        DX12Helpers::TransitionResource(cl, m_FoamMap.Get(),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }

    // -------------------------------------------------------------------------
    bool FFTOcean::CreateResources(ID3D12Device* device, ID3D12GraphicsCommandList* initCmd)
    {
        const UINT N = static_cast<UINT>(m_N);

        // Complex float texture descriptor (R32G32_FLOAT)
        auto complexDesc = DX12Helpers::Tex2DDesc(DXGI_FORMAT_R32G32_FLOAT, N, N,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

        auto MakeComplex = [&](ComPtr<ID3D12Resource>& res, const wchar_t* name)
        {
            DEMON_DX12_CHECK(device->CreateCommittedResource(
                &DX12Helpers::DefaultHeapProps,
                D3D12_HEAP_FLAG_NONE,
                &complexDesc,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                nullptr,
                IID_PPV_ARGS(&res)));
            res->SetName(name);
        };

        MakeComplex(m_H0Texture,       L"FFT_H0");
        MakeComplex(m_HtTextureDx,     L"FFT_Ht_Dx");
        MakeComplex(m_HtTextureDy,     L"FFT_Ht_Dy");
        MakeComplex(m_HtTextureDz,     L"FFT_Ht_Dz");
        MakeComplex(m_HtTextureSlopeX, L"FFT_Slope_X");
        MakeComplex(m_HtTextureSlopeZ, L"FFT_Slope_Z");
        MakeComplex(m_PingPong[0],     L"FFT_Ping");
        MakeComplex(m_PingPong[1],     L"FFT_Pong");

        // Output: displacement R16G16B16A16
        {
            auto d = DX12Helpers::Tex2DDesc(DXGI_FORMAT_R16G16B16A16_FLOAT, N, N,
                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
            DEMON_DX12_CHECK(device->CreateCommittedResource(
                &DX12Helpers::DefaultHeapProps, D3D12_HEAP_FLAG_NONE,
                &d, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr,
                IID_PPV_ARGS(&m_DisplacementMap)));
            m_DisplacementMap->SetName(L"Water_Displacement");
        }
        // Output: normals R16G16B16A16
        {
            auto d = DX12Helpers::Tex2DDesc(DXGI_FORMAT_R16G16B16A16_FLOAT, N, N,
                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
            DEMON_DX12_CHECK(device->CreateCommittedResource(
                &DX12Helpers::DefaultHeapProps, D3D12_HEAP_FLAG_NONE,
                &d, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr,
                IID_PPV_ARGS(&m_NormalMap)));
            m_NormalMap->SetName(L"Water_Normals");
        }
        // Output: foam R16_FLOAT
        {
            auto d = DX12Helpers::Tex2DDesc(DXGI_FORMAT_R16_FLOAT, N, N,
                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
            DEMON_DX12_CHECK(device->CreateCommittedResource(
                &DX12Helpers::DefaultHeapProps, D3D12_HEAP_FLAG_NONE,
                &d, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr,
                IID_PPV_ARGS(&m_FoamMap)));
            m_FoamMap->SetName(L"Water_Foam");
        }

        // Constant buffer (upload heap, persistently mapped)
        {
            auto d = DX12Helpers::BufferDesc(DX12Helpers::Align256(sizeof(FFTOceanCB)),
                D3D12_RESOURCE_FLAG_NONE);
            DEMON_DX12_CHECK(device->CreateCommittedResource(
                &DX12Helpers::UploadHeapProps, D3D12_HEAP_FLAG_NONE,
                &d, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                IID_PPV_ARGS(&m_CB)));
            m_CB->SetName(L"FFTOcean_CB");
            D3D12_RANGE r = {0,0};
            m_CB->Map(0, &r, reinterpret_cast<void**>(&m_CBData));
        }

        return true;
    }

    // -------------------------------------------------------------------------
    bool FFTOcean::CreateDescriptors(ID3D12Device* device)
    {
        // We need SRV + UAV for each intermediate texture, plus final output SRVs.
        // Use a large heap; exact count = ~20 descriptors.
        constexpr UINT kDescCount = 32;

        D3D12_DESCRIPTOR_HEAP_DESC hd{};
        hd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        hd.NumDescriptors = kDescCount;
        hd.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        DEMON_DX12_CHECK(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&m_DescHeap)));
        m_DescHeap->SetName(L"FFTOcean_DescHeap");

        m_DescSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        // Helper lambdas
        auto CPU = [&](UINT i) { return CD3DX12_CPU_DESCRIPTOR_HANDLE(
            m_DescHeap->GetCPUDescriptorHandleForHeapStart(), i, m_DescSize); };
        auto GPU = [&](UINT i) { return CD3DX12_GPU_DESCRIPTOR_HANDLE(
            m_DescHeap->GetGPUDescriptorHandleForHeapStart(), i, m_DescSize); };

        // Slot layout (arbitrary but consistent with shader registers):
        // 0  = H0  SRV
        // 1  = H0  UAV
        // 2  = Displacement SRV  <- returned to callers
        // 3  = Displacement UAV
        // 4  = Normal SRV
        // 5  = Normal UAV
        // 6  = Foam SRV
        // 7  = Foam UAV
        // 8-11 = Ht Dx/Dy/Dz/SlopeX UAV
        // 12 = SlopeZ UAV

        auto MakeSRV = [&](UINT slot, ID3D12Resource* res)
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC d{};
            d.Format                  = res->GetDesc().Format;
            d.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
            d.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            d.Texture2D.MipLevels     = 1;
            device->CreateShaderResourceView(res, &d, CPU(slot));
        };
        auto MakeUAV = [&](UINT slot, ID3D12Resource* res)
        {
            D3D12_UNORDERED_ACCESS_VIEW_DESC d{};
            d.Format        = res->GetDesc().Format;
            d.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            device->CreateUnorderedAccessView(res, nullptr, &d, CPU(slot));
        };

        MakeSRV(0,  m_H0Texture.Get());
        MakeUAV(1,  m_H0Texture.Get());
        MakeSRV(2,  m_DisplacementMap.Get());
        MakeUAV(3,  m_DisplacementMap.Get());
        MakeSRV(4,  m_NormalMap.Get());
        MakeUAV(5,  m_NormalMap.Get());
        MakeSRV(6,  m_FoamMap.Get());
        MakeUAV(7,  m_FoamMap.Get());
        MakeUAV(8,  m_HtTextureDx.Get());
        MakeUAV(9,  m_HtTextureDy.Get());
        MakeUAV(10, m_HtTextureDz.Get());
        MakeUAV(11, m_HtTextureSlopeX.Get());
        MakeUAV(12, m_HtTextureSlopeZ.Get());
        MakeSRV(13, m_ButterflyTexture ? m_ButterflyTexture.Get() : m_H0Texture.Get());

        m_DisplacementSRV = GPU(2);
        m_NormalSRV       = GPU(4);
        m_FoamSRV         = GPU(6);

        return true;
    }

    // -------------------------------------------------------------------------
    bool FFTOcean::CreatePSOs(ID3D12Device* device)
    {
        // Root signature: b0=CB, t0..t4=SRV, u0..u5=UAV
        CD3DX12_DESCRIPTOR_RANGE1 ranges[3];
        ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 8, 0, 0,
                       D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
        ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 8, 0, 0,
                       D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);

        CD3DX12_ROOT_PARAMETER1 params[3];
        params[0].InitAsConstantBufferView(0);
        params[1].InitAsDescriptorTable(1, &ranges[0]);
        params[2].InitAsDescriptorTable(1, &ranges[1]);

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rsd;
        rsd.Init_1_1(3, params, 0, nullptr,
            D3D12_ROOT_SIGNATURE_FLAG_NONE);

        ComPtr<ID3DBlob> sig, err;
        D3DX12SerializeVersionedRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1_1, &sig, &err);
        DEMON_DX12_CHECK(device->CreateRootSignature(0,
            sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&m_RS)));
        m_RS->SetName(L"FFTOcean_RS");

        // Load and compile shaders (path relative to engine shader dir)
        auto LoadCS = [&](const char* entry, ComPtr<ID3D12PipelineState>& pso) -> bool
        {
            ComPtr<ID3DBlob> cs;
            if (!DX12Helpers::CompileShader(L"Shaders/FFTOcean.hlsl", entry, "cs_6_0", cs))
            {
                DEMON_LOG_ERROR("FFTOcean: Failed to compile CS '{}'", entry);
                return false;
            }
            D3D12_COMPUTE_PIPELINE_STATE_DESC d{};
            d.pRootSignature    = m_RS.Get();
            d.CS                = { cs->GetBufferPointer(), cs->GetBufferSize() };
            DEMON_DX12_CHECK(device->CreateComputePipelineState(&d, IID_PPV_ARGS(&pso)));
            return true;
        };

        bool ok = true;
        ok &= LoadCS("CS_InitSpectrum",    m_PSO_InitSpectrum);
        ok &= LoadCS("CS_UpdateSpectrum",  m_PSO_UpdateSpectrum);
        ok &= LoadCS("CS_FFTHorizontal",   m_PSO_FFTHorizontal);
        ok &= LoadCS("CS_FFTVertical",     m_PSO_FFTVertical);
        ok &= LoadCS("CS_PermutePack",     m_PSO_PermutePack);
        ok &= LoadCS("CS_NormalGen",       m_PSO_NormalGen);
        return ok;
    }

    // -------------------------------------------------------------------------
    void FFTOcean::BakeButterfltyTexture(ID3D12Device* device,
                                         ID3D12GraphicsCommandList* cmd)
    {
        // CPU-side bake of the butterfly lookup texture (log2(N) x N x RGBA32F).
        // This is a standard Cooley-Tukey twiddle factor table.
        const int logN = Log2i(m_N);
        const UINT texW = logN;
        const UINT texH = m_N;
        const UINT pixelCount = texW * texH;

        DemonVector<std::array<float,4>> data(pixelCount);

        for (int s = 0; s < logN; ++s)
        {
            int bfSize = 1 << (s + 1);
            for (int i = 0; i < m_N; ++i)
            {
                int bfIdx = i % bfSize;
                float angle = -2.f * 3.14159265f * (float)bfIdx / (float)bfSize;
                float wr = std::cos(angle);
                float wi = std::sin(angle);

                int top = i;
                int bot = i ^ (bfSize >> 1);

                int idx = s * m_N + i;
                data[idx] = { wr, wi, (float)top, (float)bot };
            }
        }

        // Upload via upload buffer
        auto desc = DX12Helpers::Tex2DDesc(DXGI_FORMAT_R32G32B32A32_FLOAT, texW, texH,
            D3D12_RESOURCE_FLAG_NONE);
        DEMON_DX12_CHECK(device->CreateCommittedResource(
            &DX12Helpers::DefaultHeapProps, D3D12_HEAP_FLAG_NONE,
            &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(&m_ButterflyTexture)));
        m_ButterflyTexture->SetName(L"FFT_Butterfly");

        DX12Helpers::UploadTextureData(device, cmd,
            m_ButterflyTexture.Get(), data.data(),
            texW * sizeof(std::array<float,4>), texW, texH);

        DX12Helpers::TransitionResource(cmd, m_ButterflyTexture.Get(),
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }

    // -------------------------------------------------------------------------
    void FFTOcean::PassInitSpectrum(CommandList& cmd, const WaterSettings& s, float time)
    {
        auto* cl = cmd.GetCommandList();
        cl->SetPipelineState(m_PSO_InitSpectrum.Get());
        cl->SetComputeRootConstantBufferView(0, m_CB->GetGPUVirtualAddress());
        cl->SetComputeRootDescriptorTable(2, m_DescHeap->GetGPUDescriptorHandleForHeapStart());
        // Dispatch one thread per texel
        UINT groups = static_cast<UINT>(m_N) / 8;
        cl->Dispatch(groups, groups, 1);

        DX12Helpers::UAVBarrier(cl, m_H0Texture.Get());
    }

    // -------------------------------------------------------------------------
    void FFTOcean::PassUpdateSpectrum(CommandList& cmd, float time)
    {
        auto* cl = cmd.GetCommandList();
        cl->SetPipelineState(m_PSO_UpdateSpectrum.Get());
        cl->SetComputeRootConstantBufferView(0, m_CB->GetGPUVirtualAddress());
        cl->SetComputeRootDescriptorTable(1, m_DescHeap->GetGPUDescriptorHandleForHeapStart());
        cl->SetComputeRootDescriptorTable(2, m_DescHeap->GetGPUDescriptorHandleForHeapStart());
        UINT groups = static_cast<UINT>(m_N) / 8;
        cl->Dispatch(groups, groups, 1);
    }

    // -------------------------------------------------------------------------
    void FFTOcean::PassFFT(CommandList& cmd, bool horizontal,
                           ID3D12Resource* src, ID3D12Resource* dst)
    {
        auto* cl = cmd.GetCommandList();
        cl->SetPipelineState(horizontal ? m_PSO_FFTHorizontal.Get()
                                        : m_PSO_FFTVertical.Get());
        cl->SetComputeRootConstantBufferView(0, m_CB->GetGPUVirtualAddress());

        UINT groups = static_cast<UINT>(m_N) / 8;
        const int logN = Log2i(m_N);
        for (int s = 0; s < logN; ++s)
        {
            // Each pass reads src, writes to dst, then swap
            cl->Dispatch(groups, groups, 1);
            DX12Helpers::UAVBarrier(cl, dst);
            std::swap(src, dst);
        }
    }

    // -------------------------------------------------------------------------
    void FFTOcean::PassPermuteAndPack(CommandList& cmd)
    {
        auto* cl = cmd.GetCommandList();
        DX12Helpers::TransitionResource(cl, m_DisplacementMap.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        DX12Helpers::TransitionResource(cl, m_FoamMap.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        cl->SetPipelineState(m_PSO_PermutePack.Get());
        cl->SetComputeRootConstantBufferView(0, m_CB->GetGPUVirtualAddress());
        cl->SetComputeRootDescriptorTable(1, m_DescHeap->GetGPUDescriptorHandleForHeapStart());
        cl->SetComputeRootDescriptorTable(2, m_DescHeap->GetGPUDescriptorHandleForHeapStart());
        UINT groups = static_cast<UINT>(m_N) / 8;
        cl->Dispatch(groups, groups, 1);
        DX12Helpers::UAVBarrier(cl, m_DisplacementMap.Get());
        DX12Helpers::UAVBarrier(cl, m_FoamMap.Get());
    }

    // -------------------------------------------------------------------------
    void FFTOcean::PassGenerateNormals(CommandList& cmd)
    {
        auto* cl = cmd.GetCommandList();
        DX12Helpers::TransitionResource(cl, m_NormalMap.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        cl->SetPipelineState(m_PSO_NormalGen.Get());
        cl->SetComputeRootConstantBufferView(0, m_CB->GetGPUVirtualAddress());
        cl->SetComputeRootDescriptorTable(1, m_DescHeap->GetGPUDescriptorHandleForHeapStart());
        cl->SetComputeRootDescriptorTable(2, m_DescHeap->GetGPUDescriptorHandleForHeapStart());
        UINT groups = static_cast<UINT>(m_N) / 8;
        cl->Dispatch(groups, groups, 1);
        DX12Helpers::UAVBarrier(cl, m_NormalMap.Get());
    }

} // namespace Demon
