#pragma once
#include "Core/DemonCore.h"
#include "WaterComponent.h"
#include "WaterRenderer.h"
#include "FFTOcean.h"

namespace Demon
{
    class Scene;
    class CommandList;
    class Camera;

    // -------------------------------------------------------------------------
    // WaterSystem
    //   Owns all water components, drives FFT simulation, dispatches render
    //   passes in the correct frame order.
    //
    //   Frame order:
    //     1. FFTOcean::Simulate()          – compute displacement / normal maps
    //     2. WaterRenderer::RenderPlanar() – optional planar reflection RT
    //     3. WaterRenderer::RenderWater()  – main opaque + transparent pass
    //     4. WaterRenderer::RenderUnderwater() – post-fx if camera submerged
    // -------------------------------------------------------------------------
    class WaterSystem
    {
    public:
        WaterSystem();
        ~WaterSystem();

        // Lifecycle
        bool Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* initCmdList,
                        UINT backBufferWidth, UINT backBufferHeight);
        void Shutdown();

        // Called once per frame before scene render
        void Update(float deltaTime, const Camera& camera);

        // Called inside the main render loop
        void RenderPlanarReflection(CommandList& cmd, Scene& scene, const Camera& camera);
        void Render(CommandList& cmd, const Camera& camera,
                    D3D12_CPU_DESCRIPTOR_HANDLE sceneColorSRV,
                    D3D12_CPU_DESCRIPTOR_HANDLE sceneDepthSRV);

        // Component registration (called by ECS)
        WaterComponent* CreateComponent();
        void            DestroyComponent(WaterComponent* comp);

        // Settings
        WaterSettings& GetGlobalSettings() { return m_GlobalSettings; }

        // Subsystem accessors (for editor panels)
        FFTOcean&      GetFFTOcean()    { return *m_FFTOcean; }
        WaterRenderer& GetRenderer()    { return *m_Renderer; }

        bool IsEnabled() const { return m_Enabled; }
        void SetEnabled(bool e) { m_Enabled = e; }

    private:
        bool m_Enabled = true;
        float m_AccumulatedTime = 0.f;

        WaterSettings m_GlobalSettings;

        DemonUniquePtr<FFTOcean>      m_FFTOcean;
        DemonUniquePtr<WaterRenderer> m_Renderer;

        DemonVector<DemonUniquePtr<WaterComponent>> m_Components;
    };

} // namespace Demon
