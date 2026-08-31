#include "post_common.hlsl"

float3 NeighborhoodMin(float2 uv, float2 texel)
{
    float3 minColor = SampleColor(uv);
    minColor = min(minColor, SampleColor(uv + float2( texel.x, 0.0f)));
    minColor = min(minColor, SampleColor(uv + float2(-texel.x, 0.0f)));
    minColor = min(minColor, SampleColor(uv + float2(0.0f,  texel.y)));
    minColor = min(minColor, SampleColor(uv + float2(0.0f, -texel.y)));
    return minColor;
}

float3 NeighborhoodMax(float2 uv, float2 texel)
{
    float3 maxColor = SampleColor(uv);
    maxColor = max(maxColor, SampleColor(uv + float2( texel.x, 0.0f)));
    maxColor = max(maxColor, SampleColor(uv + float2(-texel.x, 0.0f)));
    maxColor = max(maxColor, SampleColor(uv + float2(0.0f,  texel.y)));
    maxColor = max(maxColor, SampleColor(uv + float2(0.0f, -texel.y)));
    return maxColor;
}

float4 PSMain(VSOut i) : SV_Target
{
    const float2 texel = InvTexSize();
    const float3 current = SampleColor(i.uv);
    const float2 velocity = SampleAux(i.uv);
    const float2 historyUv = i.uv - velocity;

    if (any(historyUv < float2(0.0f, 0.0f)) || any(historyUv > float2(1.0f, 1.0f)))
        return float4(current, 1.0f);

    const float feedback = saturate(params0.x);
    const float sharpness = saturate(params0.y);
    const float motionRejection = saturate(params0.z);

    float3 history = SampleHistory(historyUv);
    const float3 neighborhoodMin = NeighborhoodMin(i.uv, texel);
    const float3 neighborhoodMax = NeighborhoodMax(i.uv, texel);
    history = clamp(history, neighborhoodMin, neighborhoodMax);

    const float motionWeight = saturate(length(velocity) * 160.0f);
    const float blend = feedback * (1.0f - motionWeight * motionRejection);
    float3 resolved = lerp(current, history, saturate(blend));

    const float3 blur = (SampleColor(i.uv + float2(texel.x, 0.0f))
                       + SampleColor(i.uv - float2(texel.x, 0.0f))
                       + SampleColor(i.uv + float2(0.0f, texel.y))
                       + SampleColor(i.uv - float2(0.0f, texel.y))) * 0.25f;
    resolved = lerp(resolved, resolved + (resolved - blur), sharpness);

    return float4(max(resolved, 0.0f), 1.0f);
}
