#include "post_common.hlsl"

float4 PSMain(VSOut i) : SV_Target
{
    return float4(SampleColor(i.uv), 1.0f);
}
