#include "post_common.hlsl"

static const float kFarDepth = 0.999999f;

float Luma(float3 color)
{
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

float Hash12(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * 0.1031f);
    p3 += dot(p3, p3.yzx + 33.33f);
    return frac((p3.x + p3.y) * p3.z);
}

float2 Rotate(float2 v, float2 cs)
{
    return float2(v.x * cs.x - v.y * cs.y, v.x * cs.y + v.y * cs.x);
}

float2 SignNotZero(float2 v)
{
    return float2(v.x >= 0.0f ? 1.0f : -1.0f,
                  v.y >= 0.0f ? 1.0f : -1.0f);
}

float3 DecodeOctNormal(float2 encoded)
{
    float3 n = float3(encoded.x, encoded.y, 1.0f - abs(encoded.x) - abs(encoded.y));
    if (n.z < 0.0f)
        n.xy = (1.0f - abs(n.yx)) * SignNotZero(n.xy);
    return normalize(n);
}

float3 ReconstructWorldPosition(float2 uv, float depth)
{
    float4 clip = float4(uv * 2.0f - 1.0f, depth, 1.0f);
    float4 world = mul(matrix0, clip);
    return world.xyz / max(abs(world.w), 1e-5f);
}

float3 ApplySaturation(float3 color, float saturation)
{
    const float luminance = Luma(color);
    return max(lerp(float3(luminance, luminance, luminance), color, saturation), 0.0f);
}

float4 PSMain(VSOut i) : SV_Target
{
    const float radius = max(params0.x, 0.05f);
    const float intensity = max(params0.y, 0.0f);
    const float thickness = max(params0.z, 0.01f);
    const float saturation = max(params0.w, 0.0f);

    const float depth = SampleDepth(i.uv);
    const float3 sceneColor = SampleColor(i.uv);
    if (depth >= kFarDepth || intensity <= 0.0001f)
        return float4(sceneColor, 1.0f);

    const float2 invTexSize = InvTexSize();
    const float3 centerPos = ReconstructWorldPosition(i.uv, depth);
    const float3 centerNormal = DecodeOctNormal(g_AuxTex.Sample(g_Sampler, i.uv).rg);
    const float3 centerAlbedo = saturate(g_HistoryTex.Sample(g_Sampler, i.uv).rgb);

    const float viewDistance = max(distance(centerPos, frame0.xyz), 0.35f);
    const float pixelRadius = clamp(radius * 155.0f / viewDistance, 4.0f, 96.0f);
    const float2 uvRadius = pixelRadius * invTexSize;
    const float noise = Hash12(i.uv / max(invTexSize, float2(1e-5f, 1e-5f)));
    const float angle = noise * 6.2831853f;
    const float2 rotation = float2(cos(angle), sin(angle));

    static const float2 directions[8] = {
        float2( 1.0000f,  0.0000f),
        float2( 0.7071f,  0.7071f),
        float2( 0.0000f,  1.0000f),
        float2(-0.7071f,  0.7071f),
        float2(-1.0000f,  0.0000f),
        float2(-0.7071f, -0.7071f),
        float2( 0.0000f, -1.0000f),
        float2( 0.7071f, -0.7071f)
    };

    float3 bounce = 0.0f;
    float weightSum = 0.0f;

    [unroll]
    for (int directionIndex = 0; directionIndex < 8; ++directionIndex)
    {
        const float2 dir = Rotate(directions[directionIndex], rotation);
        [unroll]
        for (int stepIndex = 1; stepIndex <= 2; ++stepIndex)
        {
            const float jitter = Hash12(i.uv * 251.0f + float2(directionIndex, stepIndex));
            const float stepScale = (stepIndex + jitter * 0.55f) / 2.55f;
            const float2 sampleUv = saturate(i.uv + dir * uvRadius * stepScale);
            const float sampleDepth = SampleDepth(sampleUv);
            if (sampleDepth >= kFarDepth)
                continue;

            const float3 samplePos = ReconstructWorldPosition(sampleUv, sampleDepth);
            const float3 sampleVec = samplePos - centerPos;
            const float dist = length(sampleVec);
            if (dist <= 0.015f || dist > radius)
                continue;

            const float3 sampleDir = sampleVec / max(dist, 1e-4f);
            const float3 sampleNormal = DecodeOctNormal(g_AuxTex.Sample(g_Sampler, sampleUv).rg);
            const float3 sampleAlbedo = saturate(g_HistoryTex.Sample(g_Sampler, sampleUv).rgb);
            const float3 sampleLighting = SampleColor(sampleUv);

            const float receiverFacing = saturate(dot(centerNormal, sampleDir) * 0.45f + 0.55f);
            const float emitterFacing = saturate(dot(sampleNormal, -sampleDir) * 0.42f + 0.58f);
            const float rangeWeight = pow(saturate(1.0f - dist / radius), 1.45f);
            const float contactWeight = saturate((thickness * 1.35f) / (abs(sampleDepth - depth) + thickness));
            const float materialDelta = saturate(distance(sampleAlbedo, centerAlbedo) * 0.95f + 0.45f);
            const float weight = receiverFacing * emitterFacing * rangeWeight * contactWeight * materialDelta;

            bounce += sampleLighting * sampleAlbedo * weight;
            weightSum += weight;
        }
    }

    if (weightSum <= 1e-4f)
        return float4(sceneColor, 1.0f);

    float3 indirect = bounce / weightSum;
    indirect = ApplySaturation(indirect, saturation);

    const float shadowFill = saturate(1.45f - Luma(sceneColor) * 0.26f);
    const float3 colorBleed = indirect * max(centerAlbedo, 0.14f) * intensity * (0.38f + shadowFill * 0.82f);
    const float3 finalColor = sceneColor + min(colorBleed, 8.0f);
    return float4(max(finalColor, 0.0f), 1.0f);
}
