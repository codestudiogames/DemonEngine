#include "post_common.hlsl"

float3 ACESFilm(float3 x)
{
    const float a = 2.51f;
    const float b = 0.03f;
    const float c = 2.43f;
    const float d = 0.59f;
    const float e = 0.14f;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

float3 ReinhardToneMap(float3 color)
{
    return color / (1.0f + color);
}

float Luma(float3 color)
{
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

float GrainNoise(float2 uv)
{
    float2 p = uv / max(InvTexSize(), float2(1e-5f, 1e-5f));
    return frac(sin(dot(p, float2(12.9898f, 78.233f))) * 43758.5453f);
}

float3 ApplyWhiteBalance(float3 color, float temperature, float tint)
{
    const float3 warm = float3(1.0f + temperature * 0.10f,
                               1.0f,
                               1.0f - temperature * 0.10f);
    const float3 tintVec = float3(1.0f + tint * 0.05f,
                                  1.0f,
                                  1.0f - tint * 0.05f);
    return color * warm * tintVec;
}

float3 ApplyTonemapOperator(float3 color, uint mode)
{
    if (mode == 1u)
        return ReinhardToneMap(color);
    return ACESFilm(color);
}

float4 PSMain(VSOut i) : SV_Target
{
    const uint tonemapMode = (uint)round(params0.x);
    const float exposure = max(params0.w, 0.01f);
    const float contrast = max(params1.x, 0.0f);
    const float saturation = max(params1.y, 0.0f);
    const float vignetteStrength = saturate(params1.z);
    const float grainStrength = saturate(params1.w);
    const float temperature = params2.x;
    const float tint = params2.y;
    const float lift = params2.z;
    const float gamma = max(params2.w, 0.01f);
    const float gain = max(frame0.x, 0.01f);

    float3 color = SampleColor(i.uv) * exposure;
    color = ApplyWhiteBalance(color, temperature, tint);
    color = max(color + float3(lift, lift, lift), float3(0.0f, 0.0f, 0.0f));
    color *= gain;
    color = ApplyTonemapOperator(color, tonemapMode);

    const float luminance = Luma(color);
    color = lerp(float3(luminance, luminance, luminance), color, saturation);
    color = saturate((color - float3(0.5f, 0.5f, 0.5f)) * contrast + float3(0.5f, 0.5f, 0.5f));
    color = saturate(pow(max(color, float3(1e-4f, 1e-4f, 1e-4f)), float3(1.0f / gamma, 1.0f / gamma, 1.0f / gamma)));

    const float2 centeredUv = i.uv * 2.0f - 1.0f;
    float vignette = 1.0f - dot(centeredUv, centeredUv) * vignetteStrength;
    vignette = saturate(pow(vignette, 1.35f));
    color *= vignette;

    const float grain = (GrainNoise(i.uv) - 0.5f) * grainStrength;
    color = saturate(color + float3(grain, grain, grain));

    return float4(color, 1.0f);
}
