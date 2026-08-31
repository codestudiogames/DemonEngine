#include "post_common.hlsl"

float4 PSMain(VSOut i) : SV_Target
{
    const float intensity = max(params0.x, 0.0f);
    const float3 sceneColor = SampleColor(i.uv);
    const float3 bloom = g_AuxTex.Sample(g_Sampler, i.uv).rgb;

    const float scenePeak = max(sceneColor.r, max(sceneColor.g, sceneColor.b));
    const float bloomStrength = intensity / (1.0f + max(scenePeak - 1.0f, 0.0f) * 0.35f);
    const float3 result = sceneColor + bloom * bloomStrength;

    return float4(max(result, 0.0f), 1.0f);
}
