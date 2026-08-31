#include "post_common.hlsl"

float4 PSMain(VSOut i) : SV_Target
{
    float focusDist = params1.x;
    float focusRange = max(params1.y, 0.0001f);
    float blurStrength = params1.z;

    float depth = SampleDepth(i.uv);
    float blur = saturate(abs(depth - focusDist) / focusRange) * blurStrength;

    float2 texel = InvTexSize() * max(blur, 0.25f);
    float3 sum = 0.0f;
    sum += SampleColor(i.uv) * 0.4f;
    sum += SampleColor(i.uv + float2( texel.x, 0.0f)) * 0.15f;
    sum += SampleColor(i.uv + float2(-texel.x, 0.0f)) * 0.15f;
    sum += SampleColor(i.uv + float2(0.0f,  texel.y)) * 0.15f;
    sum += SampleColor(i.uv + float2(0.0f, -texel.y)) * 0.15f;

    return float4(sum, 1.0f);
}
