cbuffer SceneCB : register(b0)
{
    float4x4 u_viewProj;
    float4x4 u_previousViewProj;
    float4   u_lightDir;
    float4   u_lightColor;
    float4   u_ambient;
    float4   u_cameraPos;
    float4   u_skyAmbient;
    float4   u_fogColorDensity;
    float4   u_fogParams;
    float4   u_shadowSplits;
    float4   u_shadowParams0;
    float4   u_shadowParams1;
    float4   u_atmosphereParams0;
    float4   u_atmosphereParams1;
    float4   u_iblParams;
    float4   u_probeCenter;
    float4   u_probeExtents;
    float4x4 u_lightViewProj[4];
};

Texture2D    skyTex     : register(t0);
SamplerState sampLinear : register(s0);

struct PSIn
{
    float4 pos      : SV_POSITION;
    float3 worldPos : TEXCOORD0;
};

struct PSOut
{
    float4 color    : SV_Target0;
    float2 velocity : SV_Target1;
};

float3 AnalyticSky(float3 dir)
{
    const float upness = saturate(dir.y * 0.5f + 0.5f);
    const float horizon = saturate(1.0f - abs(dir.y));
    const float3 zenith = float3(0.10f, 0.18f, 0.34f);
    const float3 horizonColor = float3(0.55f, 0.62f, 0.72f);
    const float3 lightDir = normalize(-u_lightDir.xyz);
    const float sunAmount = saturate(dot(dir, lightDir));
    const float diskSize = saturate(u_atmosphereParams1.z);
    const float sunDisk = smoothstep(1.0f - diskSize * 0.22f, 1.0f, sunAmount);
    const float sunGlow = pow(sunAmount, 48.0f) * 0.65f + pow(sunAmount, 10.0f) * 0.08f;
    float3 sky = lerp(horizonColor, zenith, upness);
    sky += horizon * float3(0.26f, 0.18f, 0.08f) * u_atmosphereParams1.y;
    const float sunIntensity = saturate(u_atmosphereParams1.x / 40.0f);
    sky += saturate(u_lightColor.rgb) * (sunGlow * (0.10f + sunIntensity * 0.22f)
                                      + sunDisk * (0.28f + sunIntensity * 0.38f));
    return sky * (0.55f + u_atmosphereParams0.x * 0.25f);
}

PSOut PSMain(PSIn input)
{
    float3 dir = normalize(input.worldPos - u_cameraPos.xyz);

    float phi   = atan2(dir.z, dir.x);
    float theta = acos(clamp(dir.y, -1.0f, 1.0f));
    float2 uv;
    uv.x = (phi / (2.0f * 3.14159265f)) + 0.5f;
    uv.y = theta / 3.14159265f;

    const float intensity = max(u_skyAmbient.w, 0.001f);
    float3 sampledSky = skyTex.Sample(sampLinear, uv).rgb * intensity;
    float3 analyticSky = AnalyticSky(dir) * intensity;
    float skyBlend = saturate(u_atmosphereParams1.y);
    float3 color = lerp(sampledSky, analyticSky, skyBlend);

    if (u_fogParams.w > 0.5f && u_fogColorDensity.a > 0.0001f)
    {
        float fogStart = max(u_fogParams.z, 0.0f);
        float fogDist = max(400.0f - fogStart, 0.0f);
        float fogAmt = 1.0f - exp(-(u_fogColorDensity.a * 0.45f + u_atmosphereParams0.x * 0.015f) * fogDist * 0.18f);
        fogAmt = saturate(fogAmt);
        color = lerp(color, u_fogColorDensity.rgb, fogAmt * saturate(u_atmosphereParams1.w + 0.35f));
    }

    PSOut output;
    output.color = float4(max(color, 0.0f), 1.0f);
    output.velocity = float2(0.0f, 0.0f);
    return output;
}
