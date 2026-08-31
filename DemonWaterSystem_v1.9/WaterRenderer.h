#pragma once
#include "Core/DemonCore.h"
#include "Core/Math/DemonMath.h"
#include "WaterComponent.h"

namespace Demon
{
    class CommandList;
    class Camera;
    class FFTOcean;
    class Scene;

    // -------------------------------------------------------------------------
    // Per-frame water constant buffer (binds to b1 in Water.hlsl)
    // -------------------------------------------------------------------------
    struct WaterCB
    {
        // --- Camera ---
        Mat4  View;
        Mat4  Proj;
        Mat4  ViewProj;
        Mat4  InvViewProj;
        Vec3  CameraPos;
        float Time;

        // --- Patch ---
        float PatchSize;
        float WaterLevel;
        float Choppiness;
        float FFTNormalStrength;

        // --- Color ---
        Vec4  ShallowColor;
        Vec4  DeepColor;
        Vec4  HorizonColor;
        float DepthFadeStart;
        float DepthFadeEnd;
        float Roughness;
        float RefractionIndex;
        float RefractionStrength;
        float _pad0[3];

        // --- Reflections ---
        float SSRIntensity;
        float SSRMaxDistance;
        float SSRThickness;
        float PlanarReflBlend;

        // --- Foam ---
        float FoamThreshold;
        float FoamIntensity;
        float FoamFadeDepth;
        float _pad1;
        Vec3  FoamColor;
        float _pad2;

        // --- Normals ---
        float NormalTile0;
        float NormalTile1;
        Vec2  NormalScroll0;
        Vec2  NormalScroll1;
        float DetailNormalStrength;
        float _pad3;

        // --- Caustics ---
        float CausticsScale;
        float CausticsSpeed;
        float CausticsIntensity;
        float _pad4;

        // --- Underwater ---
        Vec3  UnderwaterFogColor;
        float UnderwaterFogDensity;
        Vec4  UnderwaterTint;
        float UnderwaterCausticIntensity;
        float _pad5[3];

        // --- Feature flags ---
        int   FoamEnabled;
        int   CausticsEnabled;
        int   SSREnabled;
        int   PlanarReflEnabled;
    };

    // -------------------------------------------------------------------------
    // WaterRenderer
    //   Manages:
    //     - Planar reflection render target + render pass
    //     - Main water draw (indexed mesh, LOD-tessellated grid)
    //     - Screen-space reflections (compute or fullscreen pass)
    //     - Underwater post-process
    // -------------------------------------------------------------------------
    class WaterRenderer
    {
    public:
        WaterRenderer();
        ~WaterRenderer();

        bool Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* initCmd,
                        UINT width, UINT height,
                        DXGI_FORMAT backBufferFmt = DXGI_FORMAT_R16G16B16A16_FLOAT);
        void Shutdown();

        void OnResize(ID3D12Device* device, UINT width, UINT height);

        // Pass 1: render scene into planar reflection RT (mirrored camera)
        // Call this BEFORE main scene render.
        void RenderPlanarReflection(CommandList& cmd, Scene& scene, const Camera& cam,
                                    float waterY, const WaterSettings& settings);

        // Pass 2: render water surface into the current back buffer / GBuffer
        void RenderWater(CommandList& cmd, const Camera& cam,
                         const WaterSettings& settings,
                         FFTOcean& fft,
                         D3D12_CPU_DESCRIPTOR_HANDLE sceneColorSRV,
                         D3D12_CPU_DESCRIPTOR_HANDLE sceneDepthSRV,
                         float waterY, float time,
                         D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle,
                         D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle);

        // Pass 3: underwater full-screen post-process (only when camera submerged)
        void RenderUnderwater(CommandList& cmd, const Camera& cam,
                              const WaterSettings& settings, float time,
                              D3D12_CPU_DESCRIPTOR_HANDLE sceneColorSRV,
                              D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle);

        // Planar RT SRV (read by water shader)
        D3D12_GPU_DESCRIPTOR_HANDLE GetPlanarReflSRV() const { return m_PlanarReflSRV; }

    private:
        bool CreatePSOs(ID3D12Device* device, DXGI_FORMAT backBufferFmt);
        bool CreatePlanarRT(ID3D12Device* device, UINT width, UINT height);
        bool CreateWaterMesh(ID3D12Device* device, ID3D12GraphicsCommandList* cmd);
        bool CreateDescriptors(ID3D12Device* device);
        bool CreateConstantBuffers(ID3D12Device* device);
        bool CreateDetailNormalMaps(ID3D12Device* device, ID3D12GraphicsCommandList* cmd);

        void UpdateCB(const Camera& cam, const WaterSettings& s,
                      float waterY, float time);
        void BindWaterDescriptors(CommandList& cmd, FFTOcean& fft,
                                  D3D12_CPU_DESCRIPTOR_HANDLE sceneColorSRV,
                                  D3D12_CPU_DESCRIPTOR_HANDLE sceneDepthSRV);

        // Constant buffer
        ComPtr<ID3D12Resource>  m_WaterCB;
        WaterCB*                m_WaterCBData = nullptr;

        // Planar reflection RT
        ComPtr<ID3D12Resource>  m_PlanarRT;
        ComPtr<ID3D12Resource>  m_PlanarDepth;
        D3D12_CPU_DESCRIPTOR_HANDLE m_PlanarRTV = {};
        D3D12_CPU_DESCRIPTOR_HANDLE m_PlanarDSV = {};
        D3D12_GPU_DESCRIPTOR_HANDLE m_PlanarReflSRV = {};

        // Water grid mesh (patch subdivided at runtime by VS)
        ComPtr<ID3D12Resource>  m_VB;
        ComPtr<ID3D12Resource>  m_IB;
        UINT                    m_IndexCount = 0;
        D3D12_VERTEX_BUFFER_VIEW m_VBV{};
        D3D12_INDEX_BUFFER_VIEW  m_IBV{};

        // Detail normal textures (2 layers, scrolled)
        ComPtr<ID3D12Resource>  m_DetailNormal0;
        ComPtr<ID3D12Resource>  m_DetailNormal1;

        // Caustics LUT texture
        ComPtr<ID3D12Resource>  m_CausticsTexture;

        // Descriptor heap
        ComPtr<ID3D12DescriptorHeap> m_SrvHeap;
        ComPtr<ID3D12DescriptorHeap> m_RtvHeap;
        ComPtr<ID3D12DescriptorHeap> m_DsvHeap;
        UINT m_SrvDescSize = 0;

        // PSOs
        ComPtr<ID3D12PipelineState> m_PSO_Water;
        ComPtr<ID3D12PipelineState> m_PSO_Underwater;
        ComPtr<ID3D12RootSignature> m_RS;

        UINT m_Width  = 0;
        UINT m_Height = 0;
    };

} // namespace Demon
