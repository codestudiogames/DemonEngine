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

cbuffer ObjectCB : register(b1)
{
    float4x4 u_model;
    float4x4 u_prevModel;
    float4   u_albedo;
    float4   u_params;
    float4   u_flags;
    float4   u_skinning;
};

Texture2D texAlbedo            : register(t0);
Texture2D texNormal            : register(t1);
Texture2D texMetallicRoughness : register(t2);
Texture2D texEmissive          : register(t3);
SamplerState sampLinear        : register(s0);

struct PSIn
{
    float4 pos      : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float3 normal   : TEXCOORD1;
    float2 uv       : TEXCOORD2;
    float4 tangent  : TEXCOORD3;
    float4 color    : COLOR0;
    float4 currentClip  : TEXCOORD4;
    float4 previousClip : TEXCOORD5;
};

struct GBufferOut
{
    float4 albedoAO : SV_Target0;
    float4 material : SV_Target1;
    float2 normalOct : SV_Target2;
    float3 emissive : SV_Target3;
    float2 velocity : SV_Target4;
};

float2 SignNotZero(float2 v)
{
    return float2(v.x >= 0.0f ? 1.0f : -1.0f,
                  v.y >= 0.0f ? 1.0f : -1.0f);
}

float2 EncodeOctNormal(float3 n)
{
    n = normalize(n);
    n /= max(abs(n.x) + abs(n.y) + abs(n.z), 1e-5f);
    float2 encoded = n.xy;
    if (n.z < 0.0f)
        encoded = (1.0f - abs(encoded.yx)) * SignNotZero(encoded);
    return encoded;
}

float2 ComputeVelocity(float4 currentClip, float4 previousClip)
{
    const float2 currentUv = currentClip.xy / max(abs(currentClip.w), 1e-5f) * 0.5f + 0.5f;
    const float2 previousUv = previousClip.xy / max(abs(previousClip.w), 1e-5f) * 0.5f + 0.5f;
    return currentUv - previousUv;
}

float3 ResolveNormal(PSIn input)
{
    float3 n = normalize(input.normal);
    if (u_flags.y > 0.5f && length(input.tangent.xyz) > 0.001f) {
        float3 t = normalize(input.tangent.xyz);
        float3 b = normalize(cross(n, t) * input.tangent.w);
        float3x3 tbn = float3x3(t, b, n);
        float3 tangentNormal = texNormal.Sample(sampLinear, input.uv).xyz * 2.0f - 1.0f;
        n = normalize(mul(tangentNormal, tbn));
    }
    return n;
}

GBufferOut PSMain(PSIn input)
{
    const float4 albedoSample = (u_flags.x > 0.5f) ? texAlbedo.Sample(sampLinear, input.uv)
                                                   : float4(1.0f, 1.0f, 1.0f, 1.0f);
    const float3 baseColor = saturate(input.color.rgb * u_albedo.rgb * albedoSample.rgb);
    const float alpha = saturate(input.color.a * u_albedo.a * albedoSample.a);

    float roughness = saturate(u_params.x);
    float metallic = saturate(u_params.y);
    if (u_flags.z > 0.5f) {
        const float3 mr = texMetallicRoughness.Sample(sampLinear, input.uv).rgb;
        roughness = saturate(mr.g);
        metallic = saturate(mr.b);
    }
    roughness = max(roughness, 0.045f);

    const float ao = saturate(u_params.w);
    const float3 emissive = (u_flags.w > 0.5f) ? texEmissive.Sample(sampLinear, input.uv).rgb : 0.0f;
    const float3 normal = ResolveNormal(input);

    GBufferOut output;
    output.albedoAO = float4(baseColor, ao);
    output.material = float4(metallic, roughness, 0.5f, alpha);
    output.normalOct = EncodeOctNormal(normal);
    output.emissive = emissive;
    output.velocity = ComputeVelocity(input.currentClip, input.previousClip);
    return output;
}
