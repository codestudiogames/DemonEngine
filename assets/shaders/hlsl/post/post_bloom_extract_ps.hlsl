#include "post_common.hlsl"

float3 PrefilterBloom(float3 color, float threshold, float softKnee)
{
    const float brightness = max(color.r, max(color.g, color.b));
    const float knee = max(threshold * max(softKnee, 0.0001f), 1e-4f);
    float soft = saturate((brightness - threshold + knee) / (2.0f * knee));
    soft = soft * soft * knee;
    const float contribution = max(brightness - threshold, soft) / max(brightness, 1e-4f);
    return color * saturate(contribution);
}

float4 PSMain(VSOut i) : SV_Target
{
    const float threshold = max(params0.x, 0.0f);
    const float softKnee = saturate(params0.y);
    return float4(PrefilterBloom(SampleColor(i.uv), threshold, softKnee), 1.0f);
}
