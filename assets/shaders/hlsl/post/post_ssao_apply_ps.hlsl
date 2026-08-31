#include "post_common.hlsl"

float4 PSMain(VSOut i) : SV_Target
{
    const float3 sceneColor = SampleColor(i.uv);
    const float ao = saturate(g_AuxTex.Sample(g_Sampler, i.uv).r);
    return float4(sceneColor * lerp(1.0f, ao, 0.86f), 1.0f);
}
