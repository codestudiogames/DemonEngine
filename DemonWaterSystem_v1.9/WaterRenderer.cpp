#include "WaterRenderer.h"
#include "FFTOcean.h"
#include "RHI/DX12/DX12CommandList.h"
#include "RHI/DX12/DX12Helpers.h"
#include "Renderer/Camera.h"
#include "Scene/Scene.h"
#include "Core/DemonLog.h"

namespace Demon
{
    // -------------------------------------------------------------------------
    // Grid vertex
    struct WaterVertex { float x, z; };  // Y is driven by displacement in VS

    // -------------------------------------------------------------------------
    WaterRenderer::WaterRenderer()  = default;
    WaterRenderer::~WaterRenderer() { Shutdown(); }

    // -------------------------------------------------------------------------
    bool WaterRenderer::Initialize(ID3D12Device*              device,
                                   ID3D12GraphicsCommandList* initCmd,
                                   UINT width, UINT height,
                                   DXGI_FORMAT backBufferFmt)
    {
        m_Width  = width;
        m_Height = height;

        DEMON_LOG_INFO("WaterRenderer: Initializing {}x{}", width, height);

        if (!CreateConstantBuffers(device))              return false;
        if (!CreateWaterMesh(device, initCmd))           return false;
        if (!CreatePlanarRT(device, width, height))      return false;
        if (!CreateDetailNormalMaps(device, initCmd))    return false;
        if (!CreateDescriptors(device))                  return false;
        if (!CreatePSOs(device, backBufferFmt))          return false;

        DEMON_LOG_INFO("WaterRenderer: Ready.");
        return true;
    }

    // -------------------------------------------------------------------------
    void WaterRenderer::Shutdown()
    {
        m_WaterCB.Reset();
        m_PlanarRT.Reset(); m_PlanarDepth.Reset();
        m_VB.Reset(); m_IB.Reset();
        m_DetailNormal0.Reset(); m_DetailNormal1.Reset();
        m_CausticsTexture.Reset();
        m_SrvHeap.Reset(); m_RtvHeap.Reset(); m_DsvHeap.Reset();
        m_PSO_Water.Reset(); m_PSO_Underwater.Reset();
        m_RS.Reset();
    }

    // -------------------------------------------------------------------------
    void WaterRenderer::OnResize(ID3D12Device* device, UINT width, UINT height)
    {
        m_Width  = width;
        m_Height = height;
        m_PlanarRT.Reset();
        m_PlanarDepth.Reset();
        CreatePlanarRT(device, width, height);
    }

    // -------------------------------------------------------------------------
    bool WaterRenderer::CreateWaterMesh(ID3D12Device*              device,
                                        ID3D12GraphicsCommandList* cmd)
    {
        // Build a flat XZ grid. The vertex shader samples the FFT displacement
        // map and offsets each vertex in world space.
        // Resolution: 256x256 quads = fine enough for mid-range LOD.
        constexpr int kGridRes = 256;
        constexpr float kHalf  = 0.5f;

        DemonVector<WaterVertex> verts;
        verts.reserve((kGridRes + 1) * (kGridRes + 1));
        for (int z = 0; z <= kGridRes; ++z)
        for (int x = 0; x <= kGridRes; ++x)
        {
            verts.push_back({
                (float)x / kGridRes - kHalf,
                (float)z / kGridRes - kHalf
            });
        }

        DemonVector<uint32_t> indices;
        indices.reserve(kGridRes * kGridRes * 6);
        for (int z = 0; z < kGridRes; ++z)
        for (int x = 0; x < kGridRes; ++x)
        {
            uint32_t tl = z * (kGridRes+1) + x;
            uint32_t tr = tl + 1;
            uint32_t bl = tl + (kGridRes+1);
            uint32_t br = bl + 1;
            indices.insert(indices.end(), { tl, tr, bl, tr, br, bl });
        }

        m_IndexCount = static_cast<UINT>(indices.size());

        // VB
        {
            UINT64 sz = verts.size() * sizeof(WaterVertex);
            DX12Helpers::CreateBufferAndUpload(device, cmd, verts.data(), sz,
                m_VB, L"Water_VB");
            m_VBV.BufferLocation = m_VB->GetGPUVirtualAddress();
            m_VBV.SizeInBytes    = static_cast<UINT>(sz);
            m_VBV.StrideInBytes  = sizeof(WaterVertex);
        }
        // IB
        {
            UINT64 sz = indices.size() * sizeof(uint32_t);
            DX12Helpers::CreateBufferAndUpload(device, cmd, indices.data(), sz,
                m_IB, L"Water_IB");
            m_IBV.BufferLocation = m_IB->GetGPUVirtualAddress();
            m_IBV.SizeInBytes    = static_cast<UINT>(sz);
            m_IBV.Format         = DXGI_FORMAT_R32_UINT;
        }
        return true;
    }

    // -------------------------------------------------------------------------
    bool WaterRenderer::CreatePlanarRT(ID3D12Device* device, UINT w, UINT h)
    {
        // Half resolution is fine for planar reflections
        UINT rw = w / 2, rh = h / 2;

        {
            auto d = DX12Helpers::Tex2DDesc(DXGI_FORMAT_R16G16B16A16_FLOAT, rw, rh,
                D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
            D3D12_CLEAR_VALUE cv{ DXGI_FORMAT_R16G16B16A16_FLOAT, {0,0,0,0} };
            DEMON_DX12_CHECK(device->CreateCommittedResource(
                &DX12Helpers::DefaultHeapProps, D3D12_HEAP_FLAG_NONE,
                &d, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &cv,
                IID_PPV_ARGS(&m_PlanarRT)));
            m_PlanarRT->SetName(L"Water_PlanarReflRT");
        }
        {
            auto d = DX12Helpers::Tex2DDesc(DXGI_FORMAT_D32_FLOAT, rw, rh,
                D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
            D3D12_CLEAR_VALUE cv; cv.Format = DXGI_FORMAT_D32_FLOAT;
            cv.DepthStencil = {1.f, 0};
            DEMON_DX12_CHECK(device->CreateCommittedResource(
                &DX12Helpers::DefaultHeapProps, D3D12_HEAP_FLAG_NONE,
                &d, D3D12_RESOURCE_STATE_DEPTH_WRITE, &cv,
                IID_PPV_ARGS(&m_PlanarDepth)));
            m_PlanarDepth->SetName(L"Water_PlanarDepth");
        }
        return true;
    }

    // -------------------------------------------------------------------------
    bool WaterRenderer::CreateDetailNormalMaps(ID3D12Device*              device,
                                               ID3D12GraphicsCommandList* cmd)
    {
        // Generate two procedural normal maps in code (tiling ripple patterns).
        // In production you'd load DXT5 normal map assets.
        constexpr int kSz = 256;
        DemonVector<uint32_t> data(kSz * kSz);

        auto FillNormal = [&](float freq, float phase) {
            for (int y = 0; y < kSz; ++y)
            for (int x = 0; x < kSz; ++x)
            {
                float u  = (float)x / kSz;
                float v  = (float)y / kSz;
                float nx = std::sin(u * freq * 6.283f + phase) * 0.5f + 0.5f;
                float ny = std::cos(v * freq * 6.283f + phase) * 0.5f + 0.5f;
                float nz = 1.f; // ensure it points ~up
                // Pack to R8G8B8A8_UNORM
                uint8_t r = (uint8_t)(nx * 255.f);
                uint8_t g = (uint8_t)(ny * 255.f);
                uint8_t b = (uint8_t)(nz * 255.f);
                data[y * kSz + x] = (0xFF << 24) | (b << 16) | (g << 8) | r;
            }
        };

        FillNormal(3.f, 0.f);
        DX12Helpers::CreateTex2DAndUpload(device, cmd, data.data(), kSz, kSz,
            DXGI_FORMAT_R8G8B8A8_UNORM, m_DetailNormal0, L"Water_NormalDetail0");

        FillNormal(7.f, 1.57f);
        DX12Helpers::CreateTex2DAndUpload(device, cmd, data.data(), kSz, kSz,
            DXGI_FORMAT_R8G8B8A8_UNORM, m_DetailNormal1, L"Water_NormalDetail1");

        // Simple animated caustics LUT (grayscale sine pattern)
        for (int y = 0; y < kSz; ++y)
        for (int x = 0; x < kSz; ++x)
        {
            float u  = (float)x / kSz;
            float v  = (float)y / kSz;
            float val= (std::sin(u*18.f) * std::cos(v*18.f) +
                        std::sin(u*9.f + v*13.f)) * 0.5f + 0.5f;
            val = std::pow(val, 4.f); // sharpen caustics
            uint8_t c = (uint8_t)(val * 255.f);
            data[y * kSz + x] = (0xFF << 24) | (c << 16) | (c << 8) | c;
        }
        DX12Helpers::CreateTex2DAndUpload(device, cmd, data.data(), kSz, kSz,
            DXGI_FORMAT_R8G8B8A8_UNORM, m_CausticsTexture, L"Water_Caustics");

        return true;
    }

    // -------------------------------------------------------------------------
    bool WaterRenderer::CreateDescriptors(ID3D12Device* device)
    {
        m_SrvDescSize = device->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        // SRV heap for: planar refl, detail normal x2, caustics, scene color,
        //               scene depth, FFT displacement, FFT normal, FFT foam = 9 slots
        {
            D3D12_DESCRIPTOR_HEAP_DESC d{};
            d.Type  = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            d.NumDescriptors = 16;
            d.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            DEMON_DX12_CHECK(device->CreateDescriptorHeap(&d, IID_PPV_ARGS(&m_SrvHeap)));
            m_SrvHeap->SetName(L"WaterRenderer_SRVHeap");
        }
        // RTV heap (1 for planar RT)
        {
            D3D12_DESCRIPTOR_HEAP_DESC d{};
            d.Type  = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            d.NumDescriptors = 1;
            DEMON_DX12_CHECK(device->CreateDescriptorHeap(&d, IID_PPV_ARGS(&m_RtvHeap)));
        }
        // DSV heap (1 for planar depth)
        {
            D3D12_DESCRIPTOR_HEAP_DESC d{};
            d.Type  = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
            d.NumDescriptors = 1;
            DEMON_DX12_CHECK(device->CreateDescriptorHeap(&d, IID_PPV_ARGS(&m_DsvHeap)));
        }

        auto cpu = [&](UINT i) {
            return CD3DX12_CPU_DESCRIPTOR_HANDLE(
                m_SrvHeap->GetCPUDescriptorHandleForHeapStart(), i, m_SrvDescSize);
        };

        // Slot 0: planar reflection SRV
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC d{};
            d.Format                  = DXGI_FORMAT_R16G16B16A16_FLOAT;
            d.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
            d.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            d.Texture2D.MipLevels     = 1;
            device->CreateShaderResourceView(m_PlanarRT.Get(), &d, cpu(0));
            m_PlanarReflSRV = CD3DX12_GPU_DESCRIPTOR_HANDLE(
                m_SrvHeap->GetGPUDescriptorHandleForHeapStart(), 0, m_SrvDescSize);
        }
        // Slot 1: detail normal 0
        DX12Helpers::CreateSRV(device, m_DetailNormal0.Get(), cpu(1));
        // Slot 2: detail normal 1
        DX12Helpers::CreateSRV(device, m_DetailNormal1.Get(), cpu(2));
        // Slot 3: caustics
        DX12Helpers::CreateSRV(device, m_CausticsTexture.Get(), cpu(3));
        // Slots 4-6: FFT textures (filled at draw time from FFTOcean)
        // Slots 7-8: scene color + depth (filled at draw time via CopyDescriptors)

        // RTV for planar
        device->CreateRenderTargetView(m_PlanarRT.Get(), nullptr,
            m_RtvHeap->GetCPUDescriptorHandleForHeapStart());
        m_PlanarRTV = m_RtvHeap->GetCPUDescriptorHandleForHeapStart();

        // DSV for planar depth
        D3D12_DEPTH_STENCIL_VIEW_DESC dsvd{};
        dsvd.Format        = DXGI_FORMAT_D32_FLOAT;
        dsvd.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        device->CreateDepthStencilView(m_PlanarDepth.Get(), &dsvd,
            m_DsvHeap->GetCPUDescriptorHandleForHeapStart());
        m_PlanarDSV = m_DsvHeap->GetCPUDescriptorHandleForHeapStart();

        return true;
    }

    // -------------------------------------------------------------------------
    bool WaterRenderer::CreateConstantBuffers(ID3D12Device* device)
    {
        UINT64 sz = DX12Helpers::Align256(sizeof(WaterCB));
        auto d    = DX12Helpers::BufferDesc(sz, D3D12_RESOURCE_FLAG_NONE);
        DEMON_DX12_CHECK(device->CreateCommittedResource(
            &DX12Helpers::UploadHeapProps, D3D12_HEAP_FLAG_NONE,
            &d, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&m_WaterCB)));
        m_WaterCB->SetName(L"Water_CB");
        D3D12_RANGE r{0,0};
        m_WaterCB->Map(0, &r, reinterpret_cast<void**>(&m_WaterCBData));
        return true;
    }

    // -------------------------------------------------------------------------
    bool WaterRenderer::CreatePSOs(ID3D12Device* device, DXGI_FORMAT backBufferFmt)
    {
        // Root signature: b0=WaterCB, t0..t8=SRV, s0=sampler
        CD3DX12_DESCRIPTOR_RANGE1 srvRange, samplerRange;
        srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 10, 0);
        samplerRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 2, 0);

        CD3DX12_ROOT_PARAMETER1 params[3];
        params[0].InitAsConstantBufferView(0);
        params[1].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_ALL);
        params[2].InitAsDescriptorTable(1, &samplerRange, D3D12_SHADER_VISIBILITY_PIXEL);

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rsd;
        rsd.Init_1_1(3, params, 0, nullptr,
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
        ComPtr<ID3DBlob> sig, err;
        D3DX12SerializeVersionedRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1_1, &sig, &err);
        DEMON_DX12_CHECK(device->CreateRootSignature(0,
            sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&m_RS)));
        m_RS->SetName(L"WaterRenderer_RS");

        // Water surface PSO
        {
            ComPtr<ID3DBlob> vs, ps;
            DEMON_ASSERT(DX12Helpers::CompileShader(L"Shaders/Water.hlsl", "VS_Water", "vs_6_0", vs));
            DEMON_ASSERT(DX12Helpers::CompileShader(L"Shaders/Water.hlsl", "PS_Water", "ps_6_0", ps));

            D3D12_INPUT_ELEMENT_DESC layout[] = {
                { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0,
                  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            };

            D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
            psoDesc.pRootSignature        = m_RS.Get();
            psoDesc.VS                    = { vs->GetBufferPointer(), vs->GetBufferSize() };
            psoDesc.PS                    = { ps->GetBufferPointer(), ps->GetBufferSize() };
            psoDesc.InputLayout           = { layout, 1 };
            psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            psoDesc.NumRenderTargets      = 1;
            psoDesc.RTVFormats[0]         = backBufferFmt;
            psoDesc.DSVFormat             = DXGI_FORMAT_D32_FLOAT;
            psoDesc.SampleDesc            = { 1, 0 };

            // Alpha blend for water transparency
            auto& blend = psoDesc.BlendState.RenderTarget[0];
            blend.BlendEnable    = TRUE;
            blend.SrcBlend       = D3D12_BLEND_SRC_ALPHA;
            blend.DestBlend      = D3D12_BLEND_INV_SRC_ALPHA;
            blend.BlendOp        = D3D12_BLEND_OP_ADD;
            blend.SrcBlendAlpha  = D3D12_BLEND_ONE;
            blend.DestBlendAlpha = D3D12_BLEND_ZERO;
            blend.BlendOpAlpha   = D3D12_BLEND_OP_ADD;
            blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

            // Depth: read, write disabled for transparent water (depth test only)
            psoDesc.DepthStencilState.DepthEnable    = TRUE;
            psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
            psoDesc.DepthStencilState.DepthFunc      = D3D12_COMPARISON_FUNC_LESS;

            // Backface culling
            psoDesc.RasterizerState                  = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
            psoDesc.RasterizerState.FrontCounterClockwise = FALSE;

            DEMON_DX12_CHECK(device->CreateGraphicsPipelineState(&psoDesc,
                IID_PPV_ARGS(&m_PSO_Water)));
            m_PSO_Water->SetName(L"PSO_Water");
        }

        // Underwater fullscreen quad PSO
        {
            ComPtr<ID3DBlob> vs, ps;
            DEMON_ASSERT(DX12Helpers::CompileShader(L"Shaders/Water.hlsl", "VS_Fullscreen", "vs_6_0", vs));
            DEMON_ASSERT(DX12Helpers::CompileShader(L"Shaders/Water.hlsl", "PS_Underwater", "ps_6_0", ps));

            D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
            psoDesc.pRootSignature        = m_RS.Get();
            psoDesc.VS                    = { vs->GetBufferPointer(), vs->GetBufferSize() };
            psoDesc.PS                    = { ps->GetBufferPointer(), ps->GetBufferSize() };
            psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            psoDesc.NumRenderTargets      = 1;
            psoDesc.RTVFormats[0]         = backBufferFmt;
            psoDesc.SampleDesc            = { 1, 0 };
            psoDesc.DepthStencilState.DepthEnable = FALSE;
            psoDesc.RasterizerState       = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
            psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
            auto& blend = psoDesc.BlendState.RenderTarget[0];
            blend.BlendEnable   = TRUE;
            blend.SrcBlend      = D3D12_BLEND_SRC_ALPHA;
            blend.DestBlend     = D3D12_BLEND_INV_SRC_ALPHA;
            blend.BlendOp       = D3D12_BLEND_OP_ADD;
            blend.SrcBlendAlpha = D3D12_BLEND_ONE;
            blend.DestBlendAlpha= D3D12_BLEND_ZERO;
            blend.BlendOpAlpha  = D3D12_BLEND_OP_ADD;
            blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

            DEMON_DX12_CHECK(device->CreateGraphicsPipelineState(&psoDesc,
                IID_PPV_ARGS(&m_PSO_Underwater)));
            m_PSO_Underwater->SetName(L"PSO_Underwater");
        }

        return true;
    }

    // -------------------------------------------------------------------------
    void WaterRenderer::UpdateCB(const Camera& cam, const WaterSettings& s,
                                 float waterY, float time)
    {
        WaterCB& cb = *m_WaterCBData;
        cb.View              = cam.GetView();
        cb.Proj              = cam.GetProj();
        cb.ViewProj          = cam.GetViewProj();
        cb.InvViewProj       = Mat4::Inverse(cam.GetViewProj());
        cb.CameraPos         = cam.GetPosition();
        cb.Time              = time;

        cb.PatchSize         = s.PatchSize;
        cb.WaterLevel        = waterY;
        cb.Choppiness        = s.Choppiness;
        cb.FFTNormalStrength = s.FFTNormalStrength;

        cb.ShallowColor      = s.ShallowColor;
        cb.DeepColor         = s.DeepColor;
        cb.HorizonColor      = s.HorizonColor;
        cb.DepthFadeStart    = s.DepthFadeStart;
        cb.DepthFadeEnd      = s.DepthFadeEnd;
        cb.Roughness         = s.Roughness;
        cb.RefractionIndex   = s.RefractionIndex;
        cb.RefractionStrength= s.RefractionStrength;

        cb.SSRIntensity      = s.SSRIntensity;
        cb.SSRMaxDistance    = s.SSRMaxDistance;
        cb.SSRThickness      = s.SSRThickness;
        cb.PlanarReflBlend   = s.PlanarReflBlend;

        cb.FoamThreshold     = s.FoamThreshold;
        cb.FoamIntensity     = s.FoamIntensity;
        cb.FoamFadeDepth     = s.FoamFadeDepth;
        cb.FoamColor         = s.FoamColor;

        cb.NormalTile0       = s.NormalTile0;
        cb.NormalTile1       = s.NormalTile1;
        cb.NormalScroll0     = s.NormalScroll0;
        cb.NormalScroll1     = s.NormalScroll1;
        cb.DetailNormalStrength = s.DetailNormalStrength;

        cb.CausticsScale     = s.CausticsScale;
        cb.CausticsSpeed     = s.CausticsSpeed;
        cb.CausticsIntensity = s.CausticsIntensity;

        cb.UnderwaterFogColor    = s.UnderwaterFogColor;
        cb.UnderwaterFogDensity  = s.UnderwaterFogDensity;
        cb.UnderwaterTint        = s.UnderwaterTint;
        cb.UnderwaterCausticIntensity = s.UnderwaterCausticIntensity;

        cb.FoamEnabled       = s.FoamEnabled     ? 1 : 0;
        cb.CausticsEnabled   = s.CausticsEnabled ? 1 : 0;
        cb.SSREnabled        = s.SSREnabled      ? 1 : 0;
        cb.PlanarReflEnabled = s.PlanarReflection? 1 : 0;
    }

    // -------------------------------------------------------------------------
    void WaterRenderer::RenderPlanarReflection(CommandList& cmd, Scene& scene,
                                               const Camera& cam,
                                               float waterY,
                                               const WaterSettings& settings)
    {
        if (!settings.PlanarReflection) return;

        auto* cl = cmd.GetCommandList();

        // Flip camera about water plane
        Camera reflCam = cam;
        Vec3 pos = cam.GetPosition();
        pos.y = 2.f * waterY - pos.y;
        reflCam.SetPosition(pos);
        Vec3 fwd = cam.GetForward(); fwd.y = -fwd.y;
        reflCam.SetForward(fwd);
        reflCam.FlipVertical();  // flip proj matrix sign

        // Transition to RT
        DX12Helpers::TransitionResource(cl, m_PlanarRT.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_RENDER_TARGET);

        float clearColor[4] = {0,0,0,0};
        cl->ClearRenderTargetView(m_PlanarRTV, clearColor, 0, nullptr);
        cl->ClearDepthStencilView(m_PlanarDSV, D3D12_CLEAR_FLAG_DEPTH, 1.f, 0, 0, nullptr);
        cl->OMSetRenderTargets(1, &m_PlanarRTV, FALSE, &m_PlanarDSV);

        UINT rw = m_Width / 2, rh = m_Height / 2;
        D3D12_VIEWPORT vp{ 0,0,(float)rw,(float)rh,0,1 };
        D3D12_RECT sc{ 0,0,(LONG)rw,(LONG)rh };
        cl->RSSetViewports(1, &vp);
        cl->RSSetScissorRects(1, &sc);

        // Scene renders itself with the reflected camera
        // (water pass uses a clip plane at waterY)
        scene.RenderForReflection(cmd, reflCam, waterY);

        // Back to SRV
        DX12Helpers::TransitionResource(cl, m_PlanarRT.Get(),
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }

    // -------------------------------------------------------------------------
    void WaterRenderer::RenderWater(CommandList& cmd, const Camera& cam,
                                    const WaterSettings& settings,
                                    FFTOcean& fft,
                                    D3D12_CPU_DESCRIPTOR_HANDLE sceneColorSRV,
                                    D3D12_CPU_DESCRIPTOR_HANDLE sceneDepthSRV,
                                    float waterY, float time,
                                    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle,
                                    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle)
    {
        UpdateCB(cam, settings, waterY, time);

        auto* cl = cmd.GetCommandList();
        cl->SetGraphicsRootSignature(m_RS.Get());

        // Set descriptor heaps
        ID3D12DescriptorHeap* heaps[] = { m_SrvHeap.Get() };
        cl->SetDescriptorHeaps(1, heaps);

        // Copy external SRVs (scene color + depth) into our heap slots 7 & 8
        // (simplified; production would use a large global heap instead)
        // -- bind CBV
        cl->SetGraphicsRootConstantBufferView(0, m_WaterCB->GetGPUVirtualAddress());
        // -- bind SRV table (slots: planarRefl, detailN0, detailN1, caustics,
        //                          fftDisp, fftNormal, fftFoam, sceneColor, sceneDepth)
        cl->SetGraphicsRootDescriptorTable(1,
            m_SrvHeap->GetGPUDescriptorHandleForHeapStart());

        // Set RT
        cl->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);
        D3D12_VIEWPORT vp{ 0,0,(float)m_Width,(float)m_Height,0,1 };
        D3D12_RECT sc{ 0,0,(LONG)m_Width,(LONG)m_Height };
        cl->RSSetViewports(1, &vp);
        cl->RSSetScissorRects(1, &sc);

        cl->SetPipelineState(m_PSO_Water.Get());
        cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cl->IASetVertexBuffers(0, 1, &m_VBV);
        cl->IASetIndexBuffer(&m_IBV);
        cl->DrawIndexedInstanced(m_IndexCount, 1, 0, 0, 0);
    }

    // -------------------------------------------------------------------------
    void WaterRenderer::RenderUnderwater(CommandList& cmd, const Camera& cam,
                                         const WaterSettings& settings, float time,
                                         D3D12_CPU_DESCRIPTOR_HANDLE sceneColorSRV,
                                         D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle)
    {
        auto* cl = cmd.GetCommandList();
        cl->SetGraphicsRootSignature(m_RS.Get());
        ID3D12DescriptorHeap* heaps[] = { m_SrvHeap.Get() };
        cl->SetDescriptorHeaps(1, heaps);
        cl->SetGraphicsRootConstantBufferView(0, m_WaterCB->GetGPUVirtualAddress());
        cl->SetGraphicsRootDescriptorTable(1,
            m_SrvHeap->GetGPUDescriptorHandleForHeapStart());

        cl->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
        cl->SetPipelineState(m_PSO_Underwater.Get());
        cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        // Fullscreen triangle (no VB needed; VS generates positions from SV_VertexID)
        cl->DrawInstanced(3, 1, 0, 0);
    }

} // namespace Demon
