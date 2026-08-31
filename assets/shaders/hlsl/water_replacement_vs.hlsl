cbuffer SceneCB : register(b0)
{
    float4x4 u_viewProj;
    float4x4 u_previousViewProj;
    float4   u_lightDir;
    float4   u_lightColor;
    float4   u_ambient;
    float4   u_cameraPos;
    float4   u_skyAmbient;
    float4   u_fogColorDensity;
    float4   u_fogParams;
    float4   u_shadowSplits;
    float4   u_shadowParams0;
    float4   u_shadowParams1;
    float4   u_atmosphereParams0;
    float4   u_atmosphereParams1;
    float4   u_iblParams;
    float4   u_probeCenter;
    float4   u_probeExtents;
    float4x4 u_lightViewProj[4];
};

cbuffer ObjectCB : register(b1)
{
    float4x4 u_model;
    float4x4 u_prevModel;
    float4   u_albedo;
    float4   u_params;   // x = waveAmplitude, y = waveLength, z = waveSpeed, w = flowSpeed
    float4   u_flags;    // x = flowDirX, y = flowDirY, z = choppiness, w = roughness
    float4   u_skinning; // x = foamIntensity, y = edgeFade, z = depth, w = highlight
};

struct VSIn
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD0;
    float4 tangent  : TANGENT;
    float4 color    : COLOR0;
    uint4  boneIndices : BLENDINDICES;
    float4 boneWeights : BLENDWEIGHT;
};

struct VSOut
{
    float4 pos          : SV_POSITION;
    float3 worldPos     : TEXCOORD0;
    float3 normal       : TEXCOORD1;
    float2 uv           : TEXCOORD2;
    float4 color        : COLOR0;
    float4 currentClip  : TEXCOORD3;
    float4 previousClip : TEXCOORD4;
    float  waveFoam     : TEXCOORD5;
};

static const float PI2 = 6.28318530718f;

float2 ResolveFlowDirection()
{
    float2 direction = u_flags.xy;
    if (dot(direction, direction) < 1e-6f)
        return float2(1.0f, 0.0f);
    return normalize(direction);
}

float WaveProfile(float phase)
{
    const float s = sin(phase);
    const float c = cos(phase);
    return s + (s * abs(c)) * (0.45f * saturate(u_flags.z * 0.5f));
}

float SampleOctave(float2 xz, float2 dir,
                   float amplitudeScale,
                   float wavelengthScale,
                   float speedScale,
                   float phaseOffset,
                   float time)
{
    const float wavelength = max(u_params.y * wavelengthScale, 0.1f);
    const float phase =
        (dot(dir, xz) / wavelength + time * (u_params.z * speedScale + u_params.w * 0.12f)) * PI2 + phaseOffset;
    return u_params.x * amplitudeScale * WaveProfile(phase);
}

float SampleWaveHeight(float2 xz, float time)
{
    if (abs(u_params.x) < 1e-4f)
        return 0.0f;

    const float2 d0 = ResolveFlowDirection();
    const float2 crossDir = float2(-d0.y, d0.x);
    const float2 d1 = normalize(d0 + crossDir * 0.45f);
    const float2 d2 = normalize(d0 - crossDir * 0.65f);
    const float2 d3 = normalize((-d0 * 0.35f) + crossDir);

    float h = 0.0f;
    h += SampleOctave(xz, d0, 1.00f, 1.00f, 1.00f, 0.0f, time);
    h += SampleOctave(xz, d1, 0.55f, 0.58f, 1.18f, 0.9f, time);
    h += SampleOctave(xz, d2, 0.32f, 0.34f, 0.82f, 1.7f, time);
    h += SampleOctave(xz, d3, 0.18f, 0.21f, 1.42f, 2.4f, time);
    return h;
}

float3 SampleWaveNormal(float2 xz, float time)
{
    const float step = max(u_params.y * 0.04f, 0.15f);
    const float hl = SampleWaveHeight(xz + float2(-step, 0.0f), time);
    const float hr = SampleWaveHeight(xz + float2( step, 0.0f), time);
    const float hd = SampleWaveHeight(xz + float2(0.0f, -step), time);
    const float hu = SampleWaveHeight(xz + float2(0.0f,  step), time);
    return normalize(float3(hl - hr, step * 2.0f, hd - hu));
}

VSOut VSMain(VSIn input)
{
    VSOut o;
    float3 localPosition = input.position;
    const float time = u_cameraPos.w;

    const float waveHeight = SampleWaveHeight(input.position.xz, time);
    localPosition.y += waveHeight;
    const float3 localNormal = SampleWaveNormal(input.position.xz, time);

    const float4 world = mul(u_model, float4(localPosition, 1.0f));
    const float4 prevWorld = mul(u_prevModel, float4(localPosition, 1.0f));
    o.currentClip = mul(u_viewProj, world);
    o.previousClip = mul(u_previousViewProj, prevWorld);
    o.pos = o.currentClip;
    o.worldPos = world.xyz;
    o.normal = mul((float3x3)u_model, localNormal);
    o.uv = input.uv;
    o.color = input.color;
    o.waveFoam = saturate(abs(waveHeight) / max(u_params.x * 2.05f, 0.001f));
    return o;
}
