#include "WaterSystem.h"
#include "Renderer/Camera.h"
#include "RHI/DX12/DX12CommandList.h"
#include "Core/DemonLog.h"

namespace Demon
{
    WaterSystem::WaterSystem()
        : m_FFTOcean(DEMON_NEW(FFTOcean))
        , m_Renderer(DEMON_NEW(WaterRenderer))
    {}

    WaterSystem::~WaterSystem() { Shutdown(); }

    // -------------------------------------------------------------------------
    bool WaterSystem::Initialize(ID3D12Device*              device,
                                 ID3D12GraphicsCommandList* initCmdList,
                                 UINT backBufferWidth,
                                 UINT backBufferHeight)
    {
        DEMON_LOG_INFO("WaterSystem: Initializing...");

        FFTSize fftSize;
        switch (m_GlobalSettings.FFTResolution)
        {
            case 128: fftSize = FFTSize::N128; break;
            case 256: fftSize = FFTSize::N256; break;
            default:  fftSize = FFTSize::N512; break;
        }

        if (!m_FFTOcean->Initialize(device, initCmdList, fftSize, m_GlobalSettings.PatchSize))
        {
            DEMON_LOG_ERROR("WaterSystem: FFTOcean init failed.");
            return false;
        }

        if (!m_Renderer->Initialize(device, initCmdList, backBufferWidth, backBufferHeight))
        {
            DEMON_LOG_ERROR("WaterSystem: WaterRenderer init failed.");
            return false;
        }

        DEMON_LOG_INFO("WaterSystem: Initialized successfully.");
        return true;
    }

    // -------------------------------------------------------------------------
    void WaterSystem::Shutdown()
    {
        m_Components.clear();
        if (m_Renderer)  m_Renderer->Shutdown();
        if (m_FFTOcean)  m_FFTOcean->Shutdown();
    }

    // -------------------------------------------------------------------------
    void WaterSystem::Update(float deltaTime, const Camera& camera)
    {
        if (!m_Enabled) return;
        m_AccumulatedTime += deltaTime * m_GlobalSettings.TimeScale;

        // Determine if any component has camera submerged
        for (auto& comp : m_Components)
        {
            if (!comp->IsEnabled()) continue;
            comp->CameraSubmerged = (camera.GetPosition().y < comp->WaterLevel);
            // Simple AABB visibility cull
            Vec3 camXZ = { camera.GetPosition().x, 0.f, camera.GetPosition().z };
            Vec3 compXZ= { comp->Position.x, 0.f, comp->Position.z };
            comp->IsVisible = (comp->IsOcean) ||
                              (length(camXZ - compXZ) < comp->Extent * 1.5f);
        }
    }

    // -------------------------------------------------------------------------
    void WaterSystem::RenderPlanarReflection(CommandList& cmd, Scene& scene,
                                              const Camera& camera)
    {
        if (!m_Enabled) return;
        for (auto& comp : m_Components)
        {
            if (!comp->IsEnabled() || !comp->IsVisible) continue;
            const WaterSettings& s = comp->OverrideSettings
                                   ? *comp->OverrideSettings
                                   : m_GlobalSettings;
            m_Renderer->RenderPlanarReflection(cmd, scene, camera, comp->WaterLevel, s);
        }
    }

    // -------------------------------------------------------------------------
    void WaterSystem::Render(CommandList& cmd, const Camera& camera,
                             D3D12_CPU_DESCRIPTOR_HANDLE sceneColorSRV,
                             D3D12_CPU_DESCRIPTOR_HANDLE sceneDepthSRV)
    {
        if (!m_Enabled) return;

        // Simulate FFT ocean (one simulation drives all ocean bodies)
        m_FFTOcean->Simulate(cmd, m_GlobalSettings, m_AccumulatedTime);

        for (auto& comp : m_Components)
        {
            if (!comp->IsEnabled() || !comp->IsVisible) continue;

            const WaterSettings& s = comp->OverrideSettings
                                   ? *comp->OverrideSettings
                                   : m_GlobalSettings;

            // TODO: pass actual RTV/DSV from frame resources
            D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = {}; // injected by renderer
            D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = {};

            m_Renderer->RenderWater(cmd, camera, s, *m_FFTOcean,
                                    sceneColorSRV, sceneDepthSRV,
                                    comp->WaterLevel, m_AccumulatedTime,
                                    rtvHandle, dsvHandle);

            if (comp->CameraSubmerged)
            {
                m_Renderer->RenderUnderwater(cmd, camera, s, m_AccumulatedTime,
                                             sceneColorSRV, rtvHandle);
            }
        }
    }

    // -------------------------------------------------------------------------
    WaterComponent* WaterSystem::CreateComponent()
    {
        auto& comp = m_Components.emplace_back(DEMON_NEW(WaterComponent));
        static uint32_t nextID = 0;
        comp->ID = nextID++;
        return comp.get();
    }

    void WaterSystem::DestroyComponent(WaterComponent* comp)
    {
        m_Components.erase(
            std::remove_if(m_Components.begin(), m_Components.end(),
                [comp](const auto& c) { return c.get() == comp; }),
            m_Components.end());
    }

} // namespace Demon
