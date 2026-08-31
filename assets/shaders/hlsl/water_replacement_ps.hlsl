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
    float4   u_params;   // x = waveAmplitude, y = waveLength, z = waveSpeed, w = flowSpeed
    float4   u_flags;    // x = flowDirX, y = flowDirY, z = choppiness, w = roughness
    float4   u_skinning; // x = foamIntensity, y = edgeFade, z = depth, w = highlight
};

struct PSIn
{
    float4 pos          : SV_POSITION;
    float3 worldPos     : TEXCOORD0;
    float3 normal       : TEXCOORD1;
    float2 uv           : TEXCOORD2;
    float4 color        : COLOR0;
    float4 currentClip  : TEXCOORD3;
    float4 previousClip : TEXCOORD4;
    float  waveFoam     : TEXCOORD5;
};

struct PSOut
{
    float4 color    : SV_Target0;
    float2 velocity : SV_Target1;
};

float2 ComputeVelocity(float4 currentClip, float4 previousClip)
{
    const float2 currentUv = currentClip.xy / max(abs(currentClip.w), 1e-5f) * 0.5f + 0.5f;
    const float2 previousUv = previousClip.xy / max(abs(previousClip.w), 1e-5f) * 0.5f + 0.5f;
    return currentUv - previousUv;
}

float GGX_D(float NdotH, float roughness)
{
    const float a = roughness * roughness;
    const float a2 = a * a;
    const float denom = (NdotH * NdotH) * (a2 - 1.0f) + 1.0f;
    return a2 / max(3.14159265f * denom * denom, 1e-4f);
}

float Fresnel(float cosTheta, float F0)
{
    return F0 + (1.0f - F0) * pow(saturate(1.0f - cosTheta), 5.0f);
}

PSOut PSMain(PSIn input)
{
    const float3 shallowColor = input.color.rgb;
    const float3 deepColor = u_albedo.rgb;
    const float transparency = saturate(u_albedo.a);
    const float choppiness = saturate(u_flags.z * 0.5f);
    const float roughness = max(u_flags.w, 0.02f);
    const float foamIntensity = saturate(u_skinning.x);
    const float edgeFade = saturate(u_skinning.y);
    const float bodyDepth = max(u_skinning.z, 0.1f);
    const float highlight = u_skinning.w;

    float3 N = normalize(input.normal);
    float3 L = normalize(-u_lightDir.xyz);
    float3 V = normalize(u_cameraPos.xyz - input.worldPos);
    float3 H = normalize(L + V);

    const float NdotL = saturate(dot(N, L));
    const float NdotV = saturate(dot(N, V));
    const float NdotH = saturate(dot(N, H));

    const float depthBlend = saturate(0.22f + bodyDepth * 0.06f + (1.0f - N.y) * 0.55f - input.waveFoam * 0.18f);
    float3 waterColor = lerp(shallowColor, deepColor, depthBlend);

    const float crest = saturate((input.waveFoam - 0.58f) * 2.8f);
    const float3 crestTint = lerp(waterColor, shallowColor + float3(0.16f, 0.20f, 0.18f), crest * (0.35f + choppiness * 0.45f));
    waterColor = lerp(waterColor, crestTint, crest);

    const float F = Fresnel(NdotV, 0.02f);
    const float D = GGX_D(NdotH, roughness);
    float3 specular = D * F * u_lightColor.rgb * (1.2f + choppiness * 0.8f);
    specular += Fresnel(NdotV, 0.04f) * max(u_skyAmbient.rgb, float3(0.04f, 0.06f, 0.08f)) * (0.75f + bodyDepth * 0.03f);

    float3 diffuse = waterColor * (u_ambient.rgb * 0.28f + u_skyAmbient.rgb * 0.72f);
    diffuse += waterColor * u_lightColor.rgb * (NdotL * 0.18f);

    const float horizonGlow = pow(saturate(1.0f - NdotV), 2.0f);
    float3 finalColor = diffuse + specular + waterColor * horizonGlow * 0.18f;

    const float2 centeredUv = abs(input.uv - 0.5f) * 2.0f;
    const float edgeDistance = max(centeredUv.x, centeredUv.y);
    const float edgeStart = saturate(1.0f - edgeFade);
    const float edgeMask = 1.0f - smoothstep(edgeStart, 1.0f, edgeDistance);

    float foam = saturate((1.0f - N.y) * (1.15f + choppiness) + crest * 0.75f) * foamIntensity;
    foam += (1.0f - edgeMask) * foamIntensity * 0.35f;
    foam = saturate(foam);
    finalColor = lerp(finalColor, float3(0.92f, 0.96f, 0.99f), foam * 0.55f);

    if (highlight > 0.5f)
        finalColor = lerp(finalColor, float3(1.12f, 0.96f, 0.72f), 0.18f);

    if (u_fogParams.w > 0.5f && u_fogColorDensity.a > 0.0001f)
    {
        const float dist = length(u_cameraPos.xyz - input.worldPos);
        const float fogHeight = u_fogParams.x;
        const float falloff = max(u_fogParams.y, 0.0001f);
        const float startDist = u_fogParams.z;
        const float heightAtten = exp(-max(input.worldPos.y - fogHeight, 0.0f) * falloff);
        float fogAmt = 1.0f - exp(-u_fogColorDensity.a * max(dist - startDist, 0.0f) * heightAtten);
        fogAmt = saturate(fogAmt);
        finalColor = lerp(finalColor, u_fogColorDensity.rgb, fogAmt);
    }

    const float alpha = saturate(transparency * input.color.a * lerp(0.55f, 0.98f, F) * edgeMask + foam * 0.16f);

    PSOut output;
    output.color = float4(max(finalColor, 0.0f), alpha);
    output.velocity = ComputeVelocity(input.currentClip, input.previousClip);
    return output;
}
