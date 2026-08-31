#pragma once
#include "Core/DemonCore.h"
#include "Core/Math/DemonMath.h"

namespace Demon
{
    // -------------------------------------------------------------------------
    // WaterSettings – shared between WaterSystem and editor panel
    // -------------------------------------------------------------------------
    struct WaterSettings
    {
        // --- Wave simulation ---
        float  WindSpeed        = 12.f;        // m/s
        float  WindDirection    = 45.f;        // degrees
        float  Fetch            = 300000.f;    // ocean fetch in metres
        float  WaveHeight       = 1.2f;        // peak-to-trough multiplier
        float  Choppiness       = 1.4f;        // Jacobian choppiness (0-2)
        float  TimeScale        = 1.f;

        // --- Surface shading ---
        Vec4   ShallowColor     = { 0.05f, 0.35f, 0.30f, 1.f };
        Vec4   DeepColor        = { 0.01f, 0.08f, 0.18f, 1.f };
        Vec4   HorizonColor     = { 0.12f, 0.25f, 0.35f, 1.f };
        float  DepthFadeStart   = 0.5f;        // metres for shallow blend
        float  DepthFadeEnd     = 8.f;
        float  Roughness        = 0.04f;       // PBR roughness
        float  RefractionIndex  = 1.333f;      // water IOR
        float  RefractionStrength = 0.04f;

        // --- Reflections ---
        bool   PlanarReflection   = true;
        float  PlanarReflBlend    = 0.9f;
        bool   SSREnabled         = true;
        float  SSRIntensity       = 0.85f;
        float  SSRMaxDistance     = 80.f;
        float  SSRThickness       = 0.5f;

        // --- Foam ---
        bool   FoamEnabled        = true;
        float  FoamThreshold      = 0.72f;     // Jacobian threshold
        float  FoamIntensity      = 1.f;
        float  FoamFadeDepth      = 0.6f;      // shore foam depth
        Vec3   FoamColor          = { 0.92f, 0.94f, 0.97f };

        // --- Caustics ---
        bool   CausticsEnabled    = true;
        float  CausticsScale      = 0.8f;
        float  CausticsSpeed      = 0.3f;
        float  CausticsIntensity  = 0.6f;

        // --- Normals ---
        float  NormalTile0        = 0.8f;      // 1st normal map UV scale
        float  NormalTile1        = 2.5f;      // 2nd normal map UV scale
        Vec2   NormalScroll0      = { 0.03f,  0.02f };
        Vec2   NormalScroll1      = { -0.02f, 0.04f };
        float  FFTNormalStrength  = 1.f;
        float  DetailNormalStrength = 0.35f;

        // --- Underwater ---
        Vec3   UnderwaterFogColor = { 0.01f, 0.12f, 0.20f };
        float  UnderwaterFogDensity = 0.18f;
        float  UnderwaterCausticIntensity = 1.2f;
        Vec4   UnderwaterTint     = { 0.05f, 0.25f, 0.45f, 1.f };

        // --- FFT grid ---
        int    FFTResolution      = 512;       // 128/256/512
        float  PatchSize          = 400.f;     // world-space metres per patch
        int    LODLevels          = 4;
        float  LODFalloff         = 2.5f;
    };

    // -------------------------------------------------------------------------
    // WaterComponent – placed on an entity in the scene
    // -------------------------------------------------------------------------
    class WaterComponent
    {
    public:
        WaterComponent();
        ~WaterComponent() = default;

        // Transform
        Vec3  Position    = { 0.f, 0.f, 0.f };
        float Extent      = 10000.f;   // half-extent in XZ (for ocean: huge)
        bool  IsOcean     = true;      // false = bounded lake/river patch

        // Per-body overrides (nullptr = use WaterSystem global)
        WaterSettings* OverrideSettings = nullptr;

        // Runtime state
        bool  IsVisible   = false;
        bool  CameraSubmerged = false;
        float WaterLevel  = 0.f;       // world-space Y

        // Unique ID
        uint32_t ID = 0;

        bool IsEnabled() const { return m_Enabled; }
        void SetEnabled(bool e) { m_Enabled = e; }

    private:
        bool     m_Enabled = true;
        uint32_t m_NextID  = 0;
    };

} // namespace Demon
