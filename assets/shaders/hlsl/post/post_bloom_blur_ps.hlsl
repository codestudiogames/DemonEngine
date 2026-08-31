#include "post_common.hlsl"

float4 PSMain(VSOut i) : SV_Target
{
    const float radius = max(params0.x, 0.5f);
    const bool horizontal = params0.y > 0.5f;
    const float2 texel = InvTexSize();
    const float2 axis = horizontal ? float2(texel.x * radius, 0.0f) : float2(0.0f, texel.y * radius);

    float3 bloom = SampleColor(i.uv) * 0.2270270270f;
    bloom += SampleColor(i.uv + axis * 1.3846153846f) * 0.3162162162f;
    bloom += SampleColor(i.uv - axis * 1.3846153846f) * 0.3162162162f;
    bloom += SampleColor(i.uv + axis * 3.2307692308f) * 0.0702702703f;
    bloom += SampleColor(i.uv - axis * 3.2307692308f) * 0.0702702703f;

    return float4(max(bloom, 0.0f), 1.0f);
}
