#include "post_common.hlsl"

float4 PSMain(VSOut i) : SV_Target
{
    const float3 baseColor = SampleColor(i.uv);
    const float shutterScale = max(params0.x, 0.0f);
    const float maxBlurPixels = max(params0.y, 0.0f);
    const int sampleCount = clamp((int)round(params0.z), 4, 16);
    const float depthThreshold = max(params0.w, 0.0001f);

    if (params2.w > 0.5f || shutterScale <= 0.001f || maxBlurPixels <= 0.25f)
        return float4(baseColor, 1.0f);

    const float2 texel = max(InvTexSize(), float2(1e-5f, 1e-5f));
    const float2 velocity = SampleAux(i.uv) * shutterScale;
    const float velocityPixels = length(velocity / texel);
    if (velocityPixels < 0.5f)
        return float4(baseColor, 1.0f);

    const float blurScale = min(velocityPixels, maxBlurPixels) / max(velocityPixels, 1e-4f);
    const float2 blurUv = velocity * blurScale;
    const float centerDepth = SampleDepth(i.uv);

    float3 accum = baseColor;
    float weightSum = 1.0f;
    const int pairCount = min(max((sampleCount + 1) / 2, 2), 8);

    [loop]
    for (int sampleIndex = 1; sampleIndex <= 8; ++sampleIndex)
    {
        if (sampleIndex > pairCount)
            break;

        const float t = (float)sampleIndex / (float)pairCount;
        const float2 offset = blurUv * (t * 0.5f);
        const float sampleWeight = 1.0f - t * 0.7f;

        const float2 uv0 = i.uv - offset;
        const float2 uv1 = i.uv + offset;

        if (all(uv0 >= float2(0.0f, 0.0f)) && all(uv0 <= float2(1.0f, 1.0f)))
        {
            const float depth0 = SampleDepth(uv0);
            const float depthWeight0 = saturate(1.0f - abs(depth0 - centerDepth) / depthThreshold);
            const float weight0 = sampleWeight * depthWeight0;
            if (weight0 > 0.0f) {
                accum += SampleColor(uv0) * weight0;
                weightSum += weight0;
            }
        }

        if (all(uv1 >= float2(0.0f, 0.0f)) && all(uv1 <= float2(1.0f, 1.0f)))
        {
            const float depth1 = SampleDepth(uv1);
            const float depthWeight1 = saturate(1.0f - abs(depth1 - centerDepth) / depthThreshold);
            const float weight1 = sampleWeight * depthWeight1;
            if (weight1 > 0.0f) {
                accum += SampleColor(uv1) * weight1;
                weightSum += weight1;
            }
        }
    }

    return float4(max(accum / max(weightSum, 1e-4f), 0.0f), 1.0f);
}
