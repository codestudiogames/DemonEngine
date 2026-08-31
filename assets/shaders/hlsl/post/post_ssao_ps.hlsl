#include "post_common.hlsl"

static const float kFarDepth = 0.999999f;

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

float3 ReconstructNormalFromDepth(float2 uv, float depth)
{
    const float2 texel = InvTexSize();
    const float3 center = ReconstructWorldPosition(uv, depth);

    const float depthRight = SampleDepth(uv + float2(texel.x, 0.0f));
    const float depthLeft = SampleDepth(uv - float2(texel.x, 0.0f));
    const float depthDown = SampleDepth(uv + float2(0.0f, texel.y));
    const float depthUp = SampleDepth(uv - float2(0.0f, texel.y));

    const float3 right = ReconstructWorldPosition(uv + float2(texel.x, 0.0f), depthRight);
    const float3 left = ReconstructWorldPosition(uv - float2(texel.x, 0.0f), depthLeft);
    const float3 down = ReconstructWorldPosition(uv + float2(0.0f, texel.y), depthDown);
    const float3 up = ReconstructWorldPosition(uv - float2(0.0f, texel.y), depthUp);

    const float3 dx = (abs(depthRight - depth) < abs(depthLeft - depth)) ? (right - center) : (center - left);
    const float3 dy = (abs(depthDown - depth) < abs(depthUp - depth)) ? (down - center) : (center - up);
    return normalize(cross(dy, dx));
}

float SampleHorizonAO(float2 uv, float3 centerPos, float3 normal, float2 offset,
                      float radius, float bias)
{
    const float2 sampleUv = saturate(uv + offset);
    const float sampleDepth = SampleDepth(sampleUv);
    if (sampleDepth >= kFarDepth)
        return 0.0f;

    const float3 samplePos = ReconstructWorldPosition(sampleUv, sampleDepth);
    const float3 sampleVec = samplePos - centerPos;
    const float dist = length(sampleVec);
    if (dist <= 1e-4f || dist >= radius)
        return 0.0f;

    const float3 sampleDir = sampleVec / dist;
    const float horizon = saturate((dot(normal, sampleDir) - bias) * 2.35f);
    const float falloff = saturate(1.0f - dist / radius);
    return horizon * falloff * falloff;
}

float4 PSMain(VSOut i) : SV_Target
{
    const float radius = max(params0.x, 0.05f);
    const float power = max(params0.y, 0.1f);
    const float bias = max(params0.z, 0.0f);
    const bool hasNormalBuffer = params0.w > 0.5f;

    const float depth = SampleDepth(i.uv);
    if (depth >= kFarDepth)
        return float4(1.0f, 1.0f, 1.0f, 1.0f);

    const float3 centerPos = ReconstructWorldPosition(i.uv, depth);
    const float3 normal = hasNormalBuffer
        ? DecodeOctNormal(g_AuxTex.Sample(g_Sampler, i.uv).rg)
        : ReconstructNormalFromDepth(i.uv, depth);

    const float2 invTexSize = InvTexSize();
    const float viewDistance = max(distance(centerPos, frame0.xyz), 0.25f);
    const float pixelRadius = clamp(radius * 90.0f / viewDistance, 2.0f, 34.0f);
    const float2 uvRadius = pixelRadius * invTexSize;

    const float angle = Hash12(i.uv / max(invTexSize, float2(1e-5f, 1e-5f))) * 6.2831853f;
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

    float occlusion = 0.0f;
    [unroll]
    for (int directionIndex = 0; directionIndex < 8; ++directionIndex)
    {
        const float2 dir = Rotate(directions[directionIndex], rotation);
        [unroll]
        for (int stepIndex = 1; stepIndex <= 3; ++stepIndex)
        {
            const float stepScale = (stepIndex + 0.35f * Hash12(i.uv * 193.0f + directionIndex)) / 3.35f;
            occlusion += SampleHorizonAO(i.uv, centerPos, normal, dir * uvRadius * stepScale,
                                         radius, bias);
        }
    }

    occlusion = saturate((occlusion / 24.0f) * power);
    const float ao = pow(saturate(1.0f - occlusion), 1.25f);
    return float4(ao, ao, ao, 1.0f);
}
