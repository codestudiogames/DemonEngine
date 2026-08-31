// =============================================================================
// Water.hlsl  –  DemonEngine Water Surface Shader
//
// Techniques:
//   VS:   FFT displacement from texture, LOD-based mesh tessellation
//   PS:   PBR shading, Fresnel, SSR, planar reflection blend,
//         refraction (scene color distortion), depth-based color,
//         Jacobian foam, caustics, shore interaction, normals blend
//   PS2:  Underwater post-process (fullscreen)
// =============================================================================

#include "WaterCommon.hlsli"

// ---- Textures ---------------------------------------------------------------
Texture2D<float4>   t_PlanarRefl  : register(t0);  // planar reflection RT
Texture2D<float4>   t_DetailN0    : register(t1);  // detail normal map 0
Texture2D<float4>   t_DetailN1    : register(t2);  // detail normal map 1
Texture2D<float4>   t_Caustics    : register(t3);  // caustics LUT
Texture2D<float4>   t_FFTDisp     : register(t4);  // FFT displacement XYZW
Texture2D<float4>   t_FFTNormal   : register(t5);  // FFT world-space normal
Texture2D<float>    t_FFTFoam     : register(t6);  // Jacobian foam map
Texture2D<float4>   t_SceneColor  : register(t7);  // resolved scene color
Texture2D<float>    t_SceneDepth  : register(t8);  // scene depth buffer

SamplerState s_Linear  : register(s0);
SamplerState s_Clamp   : register(s1);

// ---- Constant buffer --------------------------------------------------------
cbuffer WaterCB : register(b0)
{
    float4x4 g_View;
    float4x4 g_Proj;
    float4x4 g_ViewProj;
    float4x4 g_InvViewProj;
    float3   g_CameraPos;
    float    g_Time;

    float    g_PatchSize;
    float    g_WaterLevel;
    float    g_Choppiness;
    float    g_FFTNormalStrength;

    float4   g_ShallowColor;
    float4   g_DeepColor;
    float4   g_HorizonColor;
    float    g_DepthFadeStart;
    float    g_DepthFadeEnd;
    float    g_Roughness;
    float    g_RefractionIndex;
    float    g_RefractionStrength;
    float3   _pad0;

    float    g_SSRIntensity;
    float    g_SSRMaxDistance;
    float    g_SSRThickness;
    float    g_PlanarReflBlend;

    float    g_FoamThreshold;
    float    g_FoamIntensity;
    float    g_FoamFadeDepth;
    float    _pad1;
    float3   g_FoamColor;
    float    _pad2;

    float    g_NormalTile0;
    float    g_NormalTile1;
    float2   g_NormalScroll0;
    float2   g_NormalScroll1;
    float    g_DetailNormalStrength;
    float    _pad3;

    float    g_CausticsScale;
    float    g_CausticsSpeed;
    float    g_CausticsIntensity;
    float    _pad4;

    float3   g_UnderwaterFogColor;
    float    g_UnderwaterFogDensity;
    float4   g_UnderwaterTint;
    float    g_UnderwaterCausticIntensity;
    float3   _pad5;

    int      g_FoamEnabled;
    int      g_CausticsEnabled;
    int      g_SSREnabled;
    int      g_PlanarReflEnabled;
}

// ---- Math helpers -----------------------------------------------------------
static const float PI = 3.14159265359f;

// Reconstruct world position from depth
float3 ReconstructWorldPos(float2 uv, float depth)
{
    float4 ndc  = float4(uv * 2.f - 1.f, depth, 1.f);
    ndc.y       = -ndc.y;
    float4 wpos = mul(g_InvViewProj, ndc);
    return wpos.xyz / wpos.w;
}

// Schlick Fresnel approximation
float FresnelSchlick(float cosTheta, float F0)
{
    return F0 + (1.f - F0) * pow(saturate(1.f - cosTheta), 5.f);
}

// GGX specular for water surface
float GGX_Specular(float3 N, float3 H, float roughness)
{
    float a  = roughness * roughness;
    float a2 = a * a;
    float NdotH = saturate(dot(N, H));
    float denom = NdotH * NdotH * (a2 - 1.f) + 1.f;
    return a2 / (PI * denom * denom);
}

// Screen-space reflections (ray-march depth buffer)
float3 SSR(float3 worldPos, float3 reflDir, float2 screenUV)
{
    [branch]
    if (!g_SSREnabled) return float3(0,0,0);

    float3 rayPos    = worldPos;
    float3 rayStep   = reflDir * 0.5f;
    float  stepScale = 1.0f;
    float3 result    = float3(0,0,0);

    [loop]
    for (int i = 0; i < 64; ++i)
    {
        rayPos += rayStep * stepScale;
        stepScale *= 1.05f;

        // Project to screen
        float4 clipPos = mul(g_ViewProj, float4(rayPos, 1.f));
        if (clipPos.w <= 0.f) break;

        float3 ndc    = clipPos.xyz / clipPos.w;
        float2 ssUV   = float2(ndc.x * 0.5f + 0.5f, -ndc.y * 0.5f + 0.5f);
        if (any(ssUV < 0.f) || any(ssUV > 1.f)) break;

        float sceneDepth = t_SceneDepth.Sample(s_Clamp, ssUV);
        float3 scenePos  = ReconstructWorldPos(ssUV, sceneDepth);

        // Check intersection thickness
        float hitDist = scenePos.y - rayPos.y;
        if (hitDist > 0.f && hitDist < g_SSRThickness)
        {
            // Fade at screen edges
            float2 edgeFade = smoothstep(0.f, 0.1f, ssUV) * smoothstep(1.f, 0.9f, ssUV);
            float  fade     = edgeFade.x * edgeFade.y;
            result = t_SceneColor.Sample(s_Linear, ssUV).rgb * fade;
            break;
        }

        float distWorld = length(rayPos - worldPos);
        if (distWorld > g_SSRMaxDistance) break;
    }

    return result * g_SSRIntensity;
}

// Caustics: animated Voronoi-style using two offset samples
float3 SampleCaustics(float2 worldXZ, float time, float depth)
{
    [branch]
    if (!g_CausticsEnabled) return float3(1,1,1);

    float  t   = time * g_CausticsSpeed;
    float2 uv0 = worldXZ * g_CausticsScale + float2(t,  t * 0.7f);
    float2 uv1 = worldXZ * g_CausticsScale * 1.37f + float2(-t * 0.5f, t * 1.2f);

    float c0 = t_Caustics.Sample(s_Linear, uv0).r;
    float c1 = t_Caustics.Sample(s_Linear, uv1).r;

    float caustic = (c0 + c1) * 0.5f;

    // Fade caustics with depth (only shallow water)
    float depthFade = saturate(1.f - depth / g_DepthFadeEnd);
    return 1.f + caustic * g_CausticsIntensity * depthFade;
}

// =============================================================================
// VERTEX SHADER  –  Water Surface
// =============================================================================
struct VS_Input
{
    float2 xz : POSITION;
};

struct VS_Output
{
    float4 posCS  : SV_POSITION;
    float3 posWS  : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 uv     : TEXCOORD2;
    float  depth  : TEXCOORD3;   // water depth from scene depth buffer
};

VS_Output VS_Water(VS_Input vin)
{
    VS_Output vout;

    // Scale XZ by patch size and place at water level
    float2 worldXZ = vin.xz * g_PatchSize + g_CameraPos.xz; // infinite ocean trick
    float3 posWS   = float3(worldXZ.x, g_WaterLevel, worldXZ.y);

    // UV into FFT texture (wrap patch)
    float2 fftUV   = (vin.xz + 0.5f);

    // Sample displacement
    float4 disp    = t_FFTDisp.SampleLevel(s_Linear, fftUV, 0);
    posWS.x       += disp.x;
    posWS.y       += disp.y;
    posWS.z       += disp.z;

    // Normals from FFT normal map (unpacked from [0,1])
    float3 fftN    = t_FFTNormal.SampleLevel(s_Linear, fftUV, 0).xyz * 2.f - 1.f;

    vout.posCS     = mul(g_ViewProj, float4(posWS, 1.f));
    vout.posWS     = posWS;
    vout.normal    = normalize(fftN);
    vout.uv        = fftUV;
    vout.depth     = 0.f; // filled in PS from depth buffer

    return vout;
}

// =============================================================================
// PIXEL SHADER  –  Water Surface
// =============================================================================
float4 PS_Water(VS_Output pin) : SV_Target
{
    float2 screenUV = pin.posCS.xy / float2(1920.f, 1080.f); // hardcoded; use CB in prod
    screenUV.y      = 1.f - screenUV.y;

    // ----- Normals -----------------------------------------------------------
    float2 uv0 = pin.uv * g_NormalTile0 + g_NormalScroll0 * g_Time;
    float2 uv1 = pin.uv * g_NormalTile1 + g_NormalScroll1 * g_Time;
    float3 n0  = t_DetailN0.Sample(s_Linear, uv0).rgb * 2.f - 1.f;
    float3 n1  = t_DetailN1.Sample(s_Linear, uv1).rgb * 2.f - 1.f;
    float3 detN= normalize(n0 + n1) * g_DetailNormalStrength;

    // Blend FFT normal with detail normals
    float3 N   = normalize(pin.normal * g_FFTNormalStrength + detN);
    float3 V   = normalize(g_CameraPos - pin.posWS);
    float  NdotV = saturate(dot(N, V));

    // ----- Depth-based color -------------------------------------------------
    float sceneDepth  = t_SceneDepth.Sample(s_Clamp, screenUV);
    float3 sceneWPos  = ReconstructWorldPos(screenUV, sceneDepth);
    float  waterDepth = max(0.f, g_WaterLevel - sceneWPos.y);
    float  depthT     = saturate((waterDepth - g_DepthFadeStart) /
                                 (g_DepthFadeEnd - g_DepthFadeStart));

    float3 waterColor = lerp(g_ShallowColor.rgb, g_DeepColor.rgb, depthT);

    // ----- Refraction --------------------------------------------------------
    float3 refractDir  = refract(-V, N, 1.f / g_RefractionIndex);
    float2 refractUV   = screenUV + N.xz * g_RefractionStrength * (1.f - depthT);
    refractUV          = clamp(refractUV, 0.001f, 0.999f);
    float3 sceneColor  = t_SceneColor.Sample(s_Linear, refractUV).rgb;

    // Tint refracted color by water color + caustics
    float3 caustics    = SampleCaustics(pin.posWS.xz, g_Time, waterDepth);
    float3 refracted   = sceneColor * waterColor * caustics;

    // ----- Reflections -------------------------------------------------------
    float3 R           = reflect(-V, N);
    float  F0          = 0.02f; // water F0
    float  fresnel     = FresnelSchlick(NdotV, F0);

    // Planar reflection
    float3 planarRefl  = float3(0,0,0);
    [branch]
    if (g_PlanarReflEnabled)
    {
        float2 planarUV = screenUV + N.xz * 0.02f;
        planarRefl      = t_PlanarRefl.Sample(s_Linear, planarUV).rgb;
    }

    // SSR
    float3 ssrRefl     = SSR(pin.posWS, R, screenUV);

    // Blend planar + SSR
    float3 reflected   = lerp(planarRefl, ssrRefl,
                              saturate(length(ssrRefl)));
    reflected         *= g_PlanarReflBlend;

    // ----- Specular (sun) ----------------------------------------------------
    // Simple directional sun (ideally from scene light CB)
    float3 sunDir      = normalize(float3(0.4f, 0.9f, 0.3f));
    float3 H           = normalize(V + sunDir);
    float  spec        = GGX_Specular(N, H, g_Roughness);
    float3 sunColor    = float3(1.f, 0.95f, 0.8f) * 8.f;
    float3 specular    = spec * sunColor * FresnelSchlick(saturate(dot(H,V)), F0);

    // ----- Foam --------------------------------------------------------------
    float3 foamColor   = float3(0,0,0);
    [branch]
    if (g_FoamEnabled)
    {
        float jacobian = t_FFTFoam.Sample(s_Linear, pin.uv);
        float foam     = smoothstep(g_FoamThreshold - 0.1f, g_FoamThreshold, jacobian);

        // Shore foam (based on water depth)
        float shoreFoam = saturate(1.f - waterDepth / g_FoamFadeDepth);
        foam            = saturate(foam + shoreFoam * 0.6f);

        // Foam normal map distortion
        float2 foamUV   = pin.uv * 3.f + g_Time * 0.05f;
        float foamNoise = t_DetailN0.Sample(s_Linear, foamUV).r;
        foam           *= foamNoise * g_FoamIntensity;

        foamColor = g_FoamColor * foam;
        // Foam reduces fresnel (it's not specular)
        fresnel  *= (1.f - foam);
    }

    // ----- Combine -----------------------------------------------------------
    float3 finalColor  = lerp(refracted, reflected, fresnel)
                       + specular
                       + foamColor;

    // Horizon fog blend
    float  camDist     = length(pin.posWS - g_CameraPos);
    float  horizonT    = saturate(camDist / 2000.f);
    finalColor         = lerp(finalColor, g_HorizonColor.rgb, horizonT * horizonT);

    // Alpha: opaque near shore, transparent far out (optional)
    float  alpha       = saturate(depthT * 2.f + 0.4f);

    return float4(finalColor, alpha);
}

// =============================================================================
// FULLSCREEN TRIANGLE  –  common VS
// =============================================================================
struct VS_FSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };

VS_FSOut VS_Fullscreen(uint id : SV_VertexID)
{
    VS_FSOut vout;
    vout.uv  = float2((id == 1) ? 2.f : 0.f, (id == 2) ? 2.f : 0.f);
    vout.pos = float4(vout.uv.x * 2.f - 1.f, 1.f - vout.uv.y * 2.f, 0.f, 1.f);
    return vout;
}

// =============================================================================
// PIXEL SHADER  –  Underwater Post-Process
// =============================================================================
float4 PS_Underwater(VS_FSOut pin) : SV_Target
{
    float2 uv = pin.uv;

    // Distort UV with animated normal for lens-like ripple
    float2 ripple = float2(
        sin(uv.y * 8.f + g_Time * 1.5f),
        cos(uv.x * 8.f + g_Time * 1.2f)
    ) * 0.005f;
    float2 distortUV = saturate(uv + ripple);

    float3 sceneColor = t_SceneColor.Sample(s_Linear, distortUV).rgb;
    float  sceneDepth = t_SceneDepth.Sample(s_Clamp,  uv);

    // Reconstruct depth for fog
    float3 wpos      = ReconstructWorldPos(uv, sceneDepth);
    float  viewDist  = length(wpos - g_CameraPos);
    float  fog       = 1.f - exp(-g_UnderwaterFogDensity * viewDist);
    float3 foggedColor = lerp(sceneColor, g_UnderwaterFogColor, fog);

    // Color tint (simulate wavelength absorption – red fades fastest)
    float  depthAmt  = saturate(viewDist / 20.f);
    float3 absorbed  = foggedColor;
    absorbed.r      *= exp(-depthAmt * 2.0f);
    absorbed.g      *= exp(-depthAmt * 0.8f);
    // blue stays longest

    // Caustics projected onto scene
    float3 caustics  = float3(1,1,1);
    [branch]
    if (g_CausticsEnabled)
    {
        float  t   = g_Time * g_CausticsSpeed;
        float2 cuv = wpos.xz * g_CausticsScale + float2(t, t * 0.7f);
        float  c   = t_Caustics.Sample(s_Linear, cuv).r;
        caustics    = 1.f + c * g_UnderwaterCausticIntensity;
    }
    absorbed *= caustics;

    // Tint blend
    float3 finalColor = lerp(absorbed, g_UnderwaterTint.rgb, 0.15f);

    // Alpha: always fully drawn (post-process quad)
    return float4(finalColor, g_UnderwaterTint.a);
}
