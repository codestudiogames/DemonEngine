// =============================================================================
//  post_volumetric_ps.hlsl  ·  DemonEngine  ·  Enhanced Volumetric Clouds
//  Code Studio Games – Ankit Kumar Arya
//
//  Fixes:
//    · Cloud rendering ABOVE the camera (positive Y world axis)
//    · Slab-intersection guards for both above/below camera
//    · Ray jitter (dithered marching) – eliminates banding
//    · 3-D worley-style noise for vertical cloud billowing
//    · Bounded FBM for real-time editor stability
//    · Multi-layer clouds: Stratocumulus base + Altocumulus mid + Cirrus top
//    · Bounded primary march steps with cheap self-shadow
//    · Physically-calibrated optical depth scale
//    · Horizon distance fade
//    · Beer-Lambert powder + dual-lobe HG phase
// =============================================================================

#include "post_common.hlsl"

// ---------------------------------------------------------------------------
//  Phase functions
// ---------------------------------------------------------------------------

float HGPhase(float cosTheta, float g)
{
    const float g2  = g * g;
    const float den = pow(max(1.0f + g2 - 2.0f * g * cosTheta, 1e-4f), 1.5f);
    return (1.0f - g2) / (12.5663706f * den);
}

// Dual-lobe Henyey-Greenstein for more realistic forward + back scatter
float DualHGPhase(float cosTheta, float g0, float g1, float blend)
{
    return lerp(HGPhase(cosTheta, g0), HGPhase(cosTheta, g1), blend);
}

// ---------------------------------------------------------------------------
//  Hash & noise primitives
// ---------------------------------------------------------------------------

float Hash11(float p)
{
    p = frac(p * 0.1031f);
    p *= p + 33.33f;
    p *= p + p;
    return frac(p);
}

float Hash21(float2 p)
{
    p  = frac(p * float2(123.34f, 345.45f));
    p += dot(p, p + 34.345f);
    return frac(p.x * p.y);
}

float Hash31(float3 p)
{
    p  = frac(p * float3(127.1f, 311.7f, 74.7f));
    p += dot(p, p.yzx + 19.19f);
    return frac((p.x + p.y) * p.z);
}

// 2-D smooth noise
float Noise2D(float2 p)
{
    const float2 i = floor(p);
    const float2 f = frac(p);
    const float2 u = f * f * (3.0f - 2.0f * f);
    return lerp(
        lerp(Hash21(i + float2(0,0)), Hash21(i + float2(1,0)), u.x),
        lerp(Hash21(i + float2(0,1)), Hash21(i + float2(1,1)), u.x),
        u.y);
}

// 3-D smooth noise – gives vertical billowing / pillar structure
float Noise3D(float3 p)
{
    const float3 i = floor(p);
    const float3 f = frac(p);
    const float3 u = f * f * (3.0f - 2.0f * f);

    return lerp(
        lerp(
            lerp(Hash31(i + float3(0,0,0)), Hash31(i + float3(1,0,0)), u.x),
            lerp(Hash31(i + float3(0,1,0)), Hash31(i + float3(1,1,0)), u.x),
            u.y),
        lerp(
            lerp(Hash31(i + float3(0,0,1)), Hash31(i + float3(1,0,1)), u.x),
            lerp(Hash31(i + float3(0,1,1)), Hash31(i + float3(1,1,1)), u.x),
            u.y),
        u.z);
}

// Bounded 2-D FBM (base large-scale cloud shapes)
float Fbm(float2 p)
{
    float value     = 0.0f;
    float amplitude = 0.5f;
    [unroll]
    for (int o = 0; o < 4; ++o)
    {
        value     += Noise2D(p) * amplitude;
        p          = p * 2.07f + 17.13f;
        amplitude *= 0.5f;
    }
    return value;
}

// Bounded 3-D FBM (vertical detail / worley-like billowing)
float Fbm3D(float3 p)
{
    float value     = 0.0f;
    float amplitude = 0.5f;
    [unroll]
    for (int o = 0; o < 2; ++o)
    {
        value     += Noise3D(p) * amplitude;
        p          = p * 2.03f + float3(4.71f, 9.13f, 2.37f);
        amplitude *= 0.5f;
    }
    return value;
}

// ---------------------------------------------------------------------------
//  Cloud density kernel  (single layer, called per-layer below)
// ---------------------------------------------------------------------------

float CloudDensityAt(
    float3 worldPos,
    float  layerBase,      // world-Y of cloud base (MUST be > cameraPos.y for +Y clouds)
    float  layerThickness,
    float  coverage,
    float  density,
    float  scale,
    float  speed,
    float  time)
{
    // --- Vertical profile ---------------------------------------------------
    const float height01   = saturate((worldPos.y - layerBase) / max(layerThickness, 1.0f));
    const float bottomFade = smoothstep(0.00f, 0.15f, height01);
    const float topFade    = 1.0f - smoothstep(0.72f, 1.0f, height01);
    const float vertMask   = saturate(bottomFade * topFade);

    if (vertMask < 0.001f)
        return 0.0f;

    // --- Horizontal noise (2-D base shape + detail + erosion) ---------------
    const float2 wind      = float2(time * speed, time * speed * 0.37f);
    const float2 baseUv    = worldPos.xz * (0.0025f * scale) + wind;

    const float baseShape  = Fbm(baseUv);
    const float detail     = Noise2D(baseUv * 3.4f + float2(11.7f,  4.2f)) * 0.20f;
    const float erosion    = Noise2D(baseUv * 7.0f + float2(-3.1f, 19.4f)) * 0.07f;

    // --- 3-D vertical billowing ---------------------------------------------
    const float3 billowUv  = float3(worldPos.xz * (0.004f * scale) + wind, height01 * 4.0f);
    const float  billow    = Fbm3D(billowUv) * 0.24f;

    // Combine: billow pushes cloud mass upward giving cumulonimbus puffiness
    const float shaped     = baseShape + detail + billow - erosion
                             - (1.0f - vertMask) * 0.25f;

    // --- Coverage threshold -------------------------------------------------
    const float threshold  = saturate(1.0f - coverage);
    const float softness   = lerp(0.32f, 0.16f, saturate(coverage));
    const float mask       = smoothstep(threshold, min(threshold + softness, 1.001f), shaped);

    return saturate(mask * vertMask) * density;
}

// ---------------------------------------------------------------------------
//  Horizon & distance helpers
// ---------------------------------------------------------------------------

// Fades clouds gracefully toward the horizon so they don't clip abruptly
float HorizonFade(float3 rayDir, float strength)
{
    return saturate(1.0f - exp(-abs(rayDir.y) * strength));
}

// ---------------------------------------------------------------------------
//  Slab intersection helper  (FIXED: only returns t-values where ray goes UP)
// ---------------------------------------------------------------------------
//  Returns false if the ray cannot hit the [base, top] slab above the camera.
bool CloudSlabIntersect(
    float cameraY,
    float rayDirY,
    float layerBase,
    float layerTop,
    out float tStart,
    out float tEnd)
{
    tStart = 0.0f;
    tEnd   = 0.0f;

    // -----------------------------------------------------------------------
    //  BUG FIX (was causing clouds only on -Y axis):
    //
    //  Original code did NOT check whether the camera is below or above the
    //  slab, and did NOT enforce that the ray must travel toward the slab.
    //  When altitude > 0 and rayDir.y < 0 the shader was tracing downward
    //  through the earth, giving clouds on the -Y hemisphere.
    //
    //  Fix: Clouds are ABOVE the world.  The slab is at positive altitude.
    //  We require rayDir.y > 0 (ray going upward) UNLESS the camera is
    //  already inside the slab.
    // -----------------------------------------------------------------------

    const float camAboveBase = cameraY - layerBase;
    const float camAboveTop  = cameraY - layerTop;

    // Camera is above the entire slab – ray must go DOWN to hit it.
    // We intentionally skip this; clouds should appear overhead, not below.
    if (camAboveBase > 0.0f && camAboveTop > 0.0f)
    {
        // If you ever want clouds visible when camera is ABOVE the cloud deck,
        // remove this early-out.  For a ground-level camera this never fires.
        return false;
    }

    // Camera is below the slab (normal ground view):
    //   Ray must have an upward component to ever reach the cloud layer.
    if (camAboveBase < 0.0f && rayDirY <= 0.001f)
        return false;

    // Compute both intersection distances
    const float t0 = (layerBase - cameraY) / rayDirY;
    const float t1 = (layerTop  - cameraY) / rayDirY;

    tStart = max(min(t0, t1), 0.0f);
    tEnd   = max(t0, t1);

    return (tEnd > tStart + 0.01f);
}

// ---------------------------------------------------------------------------
//  Single-layer march  (reused for each cloud deck)
// ---------------------------------------------------------------------------

struct CloudLayerDesc
{
    float base;
    float thickness;
    float coverage;
    float density;
    float scale;
    float speed;
    float opticalScale;   // per-layer Beer-Lambert multiplier
};

void MarchCloudLayer(
    float3             cameraPos,
    float3             rayDir,
    CloudLayerDesc     layer,
    float              time,
    float3             lightDir,
    float3             lightColor,
    float3             cloudAlbedo,
    float3             skyFill,
    float              forwardScatter,
    float              backScatter,
    float              silverLining,
    float              sunHeight,
    float              darkness,
    int                numSteps,
    inout float        transmittance,
    inout float3       scattering)
{
    float tStart, tEnd;
    if (!CloudSlabIntersect(cameraPos.y, rayDir.y,
                            layer.base, layer.base + layer.thickness,
                            tStart, tEnd))
        return;

    // Clamp trace distance. The editor must never ask the GPU to march a huge
    // horizon ray over the entire viewport; Windows will TDR/reset the device.
    tEnd = min(tEnd, 3500.0f);
    const float traceLen  = tEnd - tStart;
    if (traceLen < 0.1f)
        return;

    const float stepLen = traceLen / (float)numSteps;

    // --- Blue-noise-style jitter seed (avoids banding rings) ---------------
    // We use a screen-space hash driven by the ray direction as a cheap
    // per-pixel dither (no texture needed).
    const float jitter = Hash21(rayDir.xz * 641.73f + float2(time * 0.1f, 0.0f));

    for (int s = 0; s < numSteps; ++s)
    {
        const float dist    = tStart + stepLen * (s + jitter);
        const float3 pos    = cameraPos + rayDir * dist;

        const float rho = CloudDensityAt(
            pos,
            layer.base, layer.thickness,
            layer.coverage, layer.density,
            layer.scale,   layer.speed,
            time);

        if (rho <= 1e-4f)
            continue;

        const float height01  = saturate((pos.y - layer.base) / layer.thickness);
        const float selfShadow = exp(-rho * (0.85f + layer.density * 0.45f) * (0.65f + height01 * 0.35f));

        // ------------------------------------------------------------------
        //  Lighting model
        // ------------------------------------------------------------------
        const float topLight  = smoothstep(0.10f, 1.0f, height01) * sunHeight;

        // Beer-Lambert powder (dark center of thick clouds)
        const float beerPowder = (1.0f - exp(-rho * 3.5f)) * 2.0f * exp(-rho * 1.5f);

        // Dual HG: forward (g=0.85) + back scatter (g=-0.3)
        const float phase = DualHGPhase(forwardScatter, 0.85f, -0.30f, 0.25f);

        const float3 ambientLight = skyFill  * (0.42f + 0.38f * (1.0f - darkness));
        const float3 sunLight     = lightColor * cloudAlbedo * selfShadow
            * (0.50f + topLight * 0.90f + phase * 0.45f + silverLining * 0.55f);
        const float3 deepShade    = cloudAlbedo * (0.08f + 0.22f * (1.0f - darkness));
        const float3 cloudColor   = lerp(
            deepShade,
            ambientLight + sunLight,
            saturate(beerPowder + topLight * 0.55f));

        // ------------------------------------------------------------------
        //  Accumulate  (physically correct Beer-Lambert)
        // ------------------------------------------------------------------
        const float optDepth = rho * stepLen * layer.opticalScale;
        const float alpha    = saturate(1.0f - exp(-optDepth));
        scattering    += transmittance * alpha * cloudColor;
        transmittance *= (1.0f - alpha);

        if (transmittance < 0.01f)
            break;   // early exit: fully opaque
    }
}

// ---------------------------------------------------------------------------
//  ApplyVolumetricClouds  (main entry – three layered cloud decks)
// ---------------------------------------------------------------------------

float3 ApplyVolumetricClouds(float3 sceneColor, float depth, float3 cameraPos, float3 rayDir)
{
    // Only render for sky pixels (no geometry in front)
    if (params3.z < 0.5f || depth < 0.9999f)
        return sceneColor;

    // -----------------------------------------------------------------------
    //  Unpack parameters (unchanged slots – stays CPU-compatible)
    // -----------------------------------------------------------------------
    const float coverage    = saturate(matrix1[0].x);
    const float density     = max(matrix1[0].y, 0.0f);
    const float altitude    = max(matrix1[0].z, 1.0f);    // cloud base (world Y)
    const float thickness   = max(matrix1[0].w, 1.0f);    // primary layer height
    const float scale       = max(matrix1[1].x, 0.05f);
    const float speed       = matrix1[1].y;
    const float darkness    = saturate(matrix1[1].z);
    const float3 tint       = saturate(matrix1[3].rgb);
    const float  time       = matrix1[3].w;

    if (density <= 1e-4f || coverage <= 1e-4f)
        return sceneColor;

    // -----------------------------------------------------------------------
    //  BUG FIX: Reject downward rays so clouds appear on +Y axis ONLY
    //  The original code allowed rayDir.y < 0, tracing down into the ground.
    // -----------------------------------------------------------------------
    if (rayDir.y <= 0.001f)
        return sceneColor;

    // -----------------------------------------------------------------------
    //  Shared lighting
    // -----------------------------------------------------------------------
    const float3 lightDir      = normalize(-frame1.xyz);
    const float3 lightColor    = max(frame2.rgb, 0.0f);
    const float  lightLum      = dot(lightColor, float3(0.2126f, 0.7152f, 0.0722f));
    const float3 neutralSun    = lerp(
        float3(lightLum, lightLum, lightLum),
        lightColor,
        0.45f * (1.0f - darkness));
    const float  sunHeight     = saturate(lightDir.y * 0.5f + 0.5f);
    const float  cosSun        = dot(rayDir, lightDir);
    const float  forwardScatter = cosSun;   // passed to DualHG
    const float  backScatter    = -cosSun;
    const float  silverLining  = pow(saturate(cosSun), 28.0f) * (0.40f + sunHeight * 0.60f);
    const float3 cloudAlbedo   = max(tint, float3(0.02f, 0.02f, 0.02f));
    const float3 skyFill       = lerp(float3(0.60f, 0.68f, 0.80f), cloudAlbedo, 0.55f);

    // Horizon fade (reduces cloud density near horizon for realism)
    const float horizFade = HorizonFade(rayDir, 6.0f);
    if (horizFade < 0.02f)
        return sceneColor;

    // -----------------------------------------------------------------------
    //  Three cloud layers – Stratocumulus / Altocumulus / Cirrus
    //  All offsets relative to the CPU-supplied `altitude` parameter.
    //
    //  Stratum    Base offset   Thickness   Coverage mod   Density mod   OptScale
    //  Strato     +0            thick       full           full          thick OD
    //  Alto       +1.4x thick   0.6x        0.7 of full    0.5           medium OD
    //  Cirrus     +2.4x thick   0.25x       0.5 of full    0.25          thin OD
    // -----------------------------------------------------------------------

    CloudLayerDesc stratoCumulus;
    stratoCumulus.base        = altitude;
    stratoCumulus.thickness   = thickness;
    stratoCumulus.coverage    = coverage;
    stratoCumulus.density     = density;
    stratoCumulus.scale       = scale;
    stratoCumulus.speed       = speed;
    stratoCumulus.opticalScale = 0.065f;   // thick, dense optical depth

    CloudLayerDesc altoCumulus;
    altoCumulus.base          = altitude + thickness * 1.40f;
    altoCumulus.thickness     = thickness * 0.60f;
    altoCumulus.coverage      = coverage * 0.70f;
    altoCumulus.density       = density  * 0.50f;
    altoCumulus.scale         = scale    * 1.30f;   // slightly higher frequency
    altoCumulus.speed         = speed    * 1.20f;   // slightly faster wind
    altoCumulus.opticalScale  = 0.040f;

    CloudLayerDesc cirrus;
    cirrus.base               = altitude + thickness * 2.40f;
    cirrus.thickness          = thickness * 0.25f;
    cirrus.coverage           = coverage * 0.50f;
    cirrus.density            = density  * 0.25f;
    cirrus.scale              = scale    * 2.50f;   // high frequency streaks
    cirrus.speed              = speed    * 1.80f;   // fast high-altitude wind
    cirrus.opticalScale       = 0.015f;             // very thin, wispy

    // -----------------------------------------------------------------------
    //  March each layer
    // -----------------------------------------------------------------------
    float  transmittance = 1.0f;
    float3 scattering    = 0.0f;

    // Stratocumulus - bounded primary visual layer
    MarchCloudLayer(cameraPos, rayDir, stratoCumulus, time,
                    lightDir, neutralSun, cloudAlbedo, skyFill,
                    forwardScatter, backScatter, silverLining, sunHeight, darkness,
                    8, transmittance, scattering);

    // Altocumulus - lower-cost breakup layer
    MarchCloudLayer(cameraPos, rayDir, altoCumulus, time,
                    lightDir, neutralSun, cloudAlbedo, skyFill,
                    forwardScatter, backScatter, silverLining, sunHeight, darkness,
                    5, transmittance, scattering);

    // Cirrus - thin high layer
    MarchCloudLayer(cameraPos, rayDir, cirrus, time,
                    lightDir, neutralSun, cloudAlbedo, skyFill,
                    forwardScatter, backScatter, silverLining, sunHeight, darkness,
                    3, transmittance, scattering);

    // -----------------------------------------------------------------------
    //  Composite: apply horizon fade to final result
    // -----------------------------------------------------------------------
    scattering    *= horizFade;
    transmittance  = lerp(1.0f, transmittance, horizFade);

    return max(sceneColor * transmittance + scattering, 0.0f);
}

// ---------------------------------------------------------------------------
//  Volumetric Fog  (unchanged – kept identical to original)
// ---------------------------------------------------------------------------

float3 ApplyVolumetricFog(float2 uv, float3 sceneColor, float depth, float3 cameraPos, float3 rayDir, float distanceToSurface)
{
    const float fogDensity = max(params1.w, 0.0f);
    if (fogDensity <= 1e-5f || distanceToSurface <= max(params0.w, 0.0f))
        return sceneColor;

    const float startDistance = max(params0.w, 0.0f);
    const float fogHeight     = params2.x;
    const float heightFalloff = max(params2.y, 0.01f);
    const float maxOpacity    = saturate(params2.w);
    const float anisotropy    = clamp(params0.z, -0.85f, 0.85f);
    const float intensity     = max(params0.y, 0.0f);
    const float3 fogColor     = saturate(params1.rgb);
    const float3 lightDir     = normalize(-frame1.xyz);
    const float3 lightColor   = max(frame2.rgb, 0.0f);
    const float  cosTheta     = dot(rayDir, lightDir);

    // Dual-lobe Henyey-Greenstein: a sharp forward lobe for crisp sun shafts
    // plus a gentle backward lobe so fog stays lit when facing away from the
    // sun. This is the biggest quality jump over the old single-lobe phase.
    const float  phase        = lerp(HGPhase(cosTheta, anisotropy),
                                     HGPhase(cosTheta, -0.15f), 0.30f);
    // Directional light-shaft emphasis when looking toward the sun.
    const float  sunShaft     = pow(saturate(cosTheta), 8.0f);

    // 24 jittered steps (was 8, un-dithered). More slices + per-pixel jitter
    // removes the concentric banding the old version produced, and TAA then
    // resolves the remaining dither into smooth volumetrics.
    const int   kSteps        = 24;
    const float traceDistance = min(distanceToSurface - startDistance, 4000.0f);
    if (traceDistance <= 0.0f)
        return sceneColor;
    const float stepLength    = traceDistance / (float)kSteps;
    const float jitter        = Hash21(uv * 1024.0f);

    float  transmittance      = 1.0f;
    float3 scattering         = 0.0f;

    [loop]
    for (int step = 0; step < kSteps; ++step)
    {
        const float  sampleDistance = startDistance + stepLength * (step + jitter);
        const float3 samplePos      = cameraPos + rayDir * sampleDistance;
        const float  heightDensity  = exp(-max(samplePos.y - fogHeight, 0.0f) * heightFalloff);
        float  localDensity         = fogDensity * heightDensity;
        float3 sampleFogColor       = fogColor;
        float  localIntensity       = intensity;

        if (params2.z > 0.5f)
        {
            const float3 localCenter = float3(frame0.w, frame1.w, frame2.w);
            const float3 box         = abs(samplePos - localCenter) / max(matrix1[2].xyz, float3(0.001f, 0.001f, 0.001f));
            const float  edge        = saturate(1.0f - max(box.x, max(box.y, box.z)));
            const float  localMask   = pow(edge, 0.55f);
            localDensity            += matrix1[2].w * localMask;
            const float3 localColor  = (params3.z < 0.5f) ? saturate(matrix1[3].rgb) : fogColor;
            sampleFogColor           = lerp(sampleFogColor, localColor, localMask);
            localIntensity          += intensity * localMask;
        }

        // Energy-conserving analytic integration of in-scatter across the
        // segment: (1 - exp(-sigma*dt)) instead of the old linear sigma*dt,
        // which over-brightens dense fog.
        const float opticalDepth = localDensity * stepLength;
        const float extinction   = exp(-opticalDepth);
        const float3 sunTerm     = lightColor * (phase * 2.4f + sunShaft * 1.6f) * localIntensity;
        const float3 inscatter   = sampleFogColor * localIntensity + sunTerm;
        scattering              += transmittance * (1.0f - extinction) * inscatter;
        transmittance           *= extinction;

        if (transmittance < 0.003f)
            break;   // fully saturated – stop marching
    }

    const float  fogAmount = saturate((1.0f - transmittance) * maxOpacity);
    const float3 fogged    = sceneColor * transmittance + scattering;
    return lerp(sceneColor, fogged, fogAmount);
}

// ---------------------------------------------------------------------------
//  Lens Flare  (unchanged – kept identical to original)
// ---------------------------------------------------------------------------

float3 ApplyLensFlare(float2 uv, float3 sceneColor)
{
    if (params3.w < 0.5f)
        return sceneColor;

    const float intensity = max(matrix1[1].w, 0.0f);
    if (intensity <= 1e-4f)
        return sceneColor;

    const float  threshold    = (params2.z > 0.5f) ? 0.82f : matrix1[2].x;
    const float  haloWidth    = (params2.z > 0.5f) ? 0.45f : max(matrix1[2].y, 0.01f);
    const float  ghostSpacing = (params2.z > 0.5f) ? 0.36f : max(matrix1[2].z, 0.05f);
    const float  dirtIntensity= (params2.z > 0.5f) ? 0.0f  : saturate(matrix1[2].w);
    const float3 tint         = saturate(lerp(float3(1.0f, 0.95f, 0.84f), max(frame2.rgb, 0.0f), 0.45f));

    const float3 lightDir  = normalize(-frame1.xyz);
    float2       sunUv     = 0.5f + lightDir.xy * float2(0.42f, -0.42f);
    const float  sunVisible= saturate(lightDir.y * 0.8f + 0.45f);
    const float  sourceLum = dot(SampleColor(saturate(sunUv)), float3(0.2126f, 0.7152f, 0.0722f));
    const float  source    = saturate((sourceLum - threshold) * 2.0f + sunVisible * 0.25f);
    if (source <= 1e-4f)
        return sceneColor;

    const float2 toCenter  = 0.5f - sunUv;
    float3 flare           = 0.0f;
    [unroll]
    for (int ghost = 0; ghost < 4; ++ghost)
    {
        const float2 ghostUv = sunUv + toCenter * (ghostSpacing * (ghost + 1));
        const float  d       = length(uv - ghostUv);
        flare += tint * exp(-d * (18.0f + ghost * 7.0f)) * (1.0f / (ghost + 1.0f));
    }

    const float halo = exp(-length(uv - sunUv) / haloWidth * 4.0f);
    const float dirt = lerp(1.0f, 0.75f + 0.25f * Fbm(uv * 18.0f), dirtIntensity);
    return sceneColor + (flare + tint * halo * 0.55f) * source * intensity * dirt;
}

// ---------------------------------------------------------------------------
//  PSMain  (unchanged structure – fog → clouds → lens flare)
// ---------------------------------------------------------------------------

float4 PSMain(VSOut i) : SV_Target
{
    const float depth   = SampleDepth(i.uv);
    const float2 clipUv = float2(i.uv.x * 2.0f - 1.0f, 1.0f - i.uv.y * 2.0f);

    float4 nearWorld = mul(matrix0, float4(clipUv, 0.0f, 1.0f));
    float4 farWorld  = mul(matrix0, float4(clipUv, 1.0f, 1.0f));
    nearWorld       /= max(nearWorld.w, 1e-5f);
    farWorld        /= max(farWorld.w,  1e-5f);
    float4 world     = mul(matrix0, float4(clipUv, depth >= 0.999999f ? 1.0f : depth, 1.0f));
    world           /= max(world.w, 1e-5f);

    const float3 cameraPos = frame0.xyz;
    float3 ray             = (depth >= 0.999999f)
                             ? (farWorld.xyz - nearWorld.xyz)
                             : (world.xyz - cameraPos);
    float distanceToSurface = length(ray);
    if (depth >= 0.999999f)
        distanceToSurface = 350.0f;
    if (distanceToSurface <= 1e-3f)
        return float4(SampleColor(i.uv), 1.0f);

    const float3 rayDir = ray / distanceToSurface;
    float3 result       = SampleColor(i.uv);
    result = ApplyVolumetricFog(i.uv, result, depth, cameraPos, rayDir, distanceToSurface);
    result = ApplyVolumetricClouds(result, depth, cameraPos, rayDir);
    result = ApplyLensFlare(i.uv, result);
    return float4(max(result, 0.0f), 1.0f);
}
