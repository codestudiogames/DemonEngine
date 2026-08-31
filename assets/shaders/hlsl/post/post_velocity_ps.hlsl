#include "post_common.hlsl"

float4 PSMain(VSOut i) : SV_Target
{
    const float depth = SampleDepth(i.uv);
    if (depth >= 0.999999f)
        return float4(0.0f, 0.0f, 0.0f, 0.0f);

    float4 currentClip = float4(i.uv * 2.0f - 1.0f, depth, 1.0f);
    float4 world = mul(matrix0, currentClip);
    world /= max(world.w, 1e-5f);

    float4 previousClip = mul(matrix1, world);
    previousClip /= max(previousClip.w, 1e-5f);
    float2 previousUv = previousClip.xy * 0.5f + 0.5f;
    float2 velocity = i.uv - previousUv;

    return float4(velocity, 0.0f, 1.0f);
}
