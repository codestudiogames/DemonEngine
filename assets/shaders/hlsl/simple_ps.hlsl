struct LocalLight
{
    float4 positionRange;
    float4 directionType;
    float4 colorIntensity;
    float4 spotParams;
};

struct DDGIProbe
{
    float4 positionRadius;
    float4 irradiance;
    float4 visibility;
};

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
    float4   u_giParams;
    float4   u_giColor;
    float4   u_localLightCount;
    LocalLight u_localLights[16];
    float4   u_ddgiParams;
    float4   u_ddgiParams1;
    DDGIProbe u_ddgiProbes[32];
    float4x4 u_lightViewProj[4];
    float4   u_shadowDepthRange;  // per cascade: world units spanned by [0,1] depth
    float4   u_shadowTexelWorld;  // per cascade: world units covered by one texel
};

cbuffer ObjectCB : register(b1)
{
    float4x4 u_model;
    float4x4 u_prevModel;
    float4   u_albedo;
    float4   u_params;
    float4   u_flags;
};

Texture2D texAlbedo            : register(t0);
Texture2D texNormal            : register(t1);
Texture2D texMetallicRoughness : register(t2);
Texture2D texEmissive          : register(t3);
Texture2D texShadowAtlas       : register(t4);
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

struct PSOut
{
    float4 color    : SV_Target0;
    float2 velocity : SV_Target1;
};

static const float PI = 3.14159265f;

float DistributionGGX(float3 N, float3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = saturate(dot(N, H));
    float NdotH2 = NdotH * NdotH;
    float denom = NdotH2 * (a2 - 1.0f) + 1.0f;
    return a2 / max(PI * denom * denom, 1e-4f);
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0f;
    float k = (r * r) * 0.125f;
    return NdotV / lerp(NdotV, 1.0f, k);
}

float GeometrySmith(float3 N, float3 V, float3 L, float roughness)
{
    float NdotV = saturate(dot(N, V));
    float NdotL = saturate(dot(N, L));
    return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
}

float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0f - F0) * pow(1.0f - cosTheta, 5.0f);
}

// Fresnel with a roughness-aware ceiling so rough surfaces don't over-reflect
// at grazing angles (used for image-based ambient specular).
float3 FresnelSchlickRoughness(float cosTheta, float3 F0, float roughness)
{
    const float3 Fr = max(float3(1.0f - roughness, 1.0f - roughness, 1.0f - roughness), F0);
    return F0 + (Fr - F0) * pow(saturate(1.0f - cosTheta), 5.0f);
}

// Karis' analytic environment-BRDF approximation ("Physically Based Shading on
// Mobile"). Returns the pre-integrated (scale, bias) split-sum term so we get
// plausible specular occlusion without a BRDF LUT texture.
float2 EnvBRDFApproxAB(float roughness, float NdotV)
{
    const float4 c0 = float4(-1.0f, -0.0275f, -0.572f, 0.022f);
    const float4 c1 = float4( 1.0f,  0.0425f,  1.040f, -0.04f);
    const float4 r = roughness * c0 + c1;
    const float a004 = min(r.x * r.x, exp2(-9.28f * NdotV)) * r.x + r.y;
    return float2(-1.04f, 1.04f) * a004 + r.zw;
}

float3 EnvBRDFApprox(float3 F0, float roughness, float NdotV)
{
    const float2 AB = EnvBRDFApproxAB(roughness, NdotV);
    return F0 * AB.x + AB.y;
}

float4 CascadeTransform(int cascade)
{
    if (cascade == 0) return float4(0.5f, 0.5f, 0.0f, 0.0f);
    if (cascade == 1) return float4(0.5f, 0.5f, 0.5f, 0.0f);
    if (cascade == 2) return float4(0.5f, 0.5f, 0.0f, 0.5f);
    return float4(0.5f, 0.5f, 0.5f, 0.5f);
}

int SelectCascade(float3 worldPos)
{
    float cameraDist = length(worldPos - u_cameraPos.xyz);
    if (cameraDist <= u_shadowSplits.x) return 0;
    if (cameraDist <= u_shadowSplits.y) return 1;
    if (cameraDist <= u_shadowSplits.z) return 2;
    return 3;
}

// Per-cascade scalars are packed one per component. Selected with branches rather
// than dynamic vector indexing so the generated code stays predictable.
float CascadeScalar(float4 packed, int cascade)
{
    if (cascade == 0) return packed.x;
    if (cascade == 1) return packed.y;
    if (cascade == 2) return packed.z;
    return packed.w;
}

float SampleShadow(float3 worldPos, float3 normal, float3 lightDir)
{
    if (u_shadowParams0.x < 0.5f)
        return 1.0f;

    const int cascade = SelectCascade(worldPos);
    const float4 cascadeXform = CascadeTransform(cascade);

    // Bias is authored in world units and the normal offset in shadow texels, so
    // both have to be converted using this cascade's scale. Applying a fixed
    // [0,1]-depth bias instead makes it ~5x too strong in the far cascade and too
    // weak in the near one, which erases contact shadows at one end and produces
    // acne at the other.
    const float depthRange = max(CascadeScalar(u_shadowDepthRange, cascade), 1e-4f);
    const float texelWorld = max(CascadeScalar(u_shadowTexelWorld, cascade), 1e-5f);
    const float normalFacing = saturate(dot(normal, lightDir));
    const float slopeScale = lerp(2.4f, 1.0f, normalFacing);

    float3 offsetWorldPos = worldPos + normal * (u_shadowParams1.y * texelWorld * slopeScale);
    float4 shadowClip = mul(u_lightViewProj[cascade], float4(offsetWorldPos, 1.0f));
    shadowClip /= max(shadowClip.w, 1e-5f);

    // D3D12: NDC +Y is up but texture V grows downward, so the vertical axis
    // must be flipped when converting the light-space clip position to an atlas
    // UV. Sampling without this flip reads the mirrored texel and detaches the
    // shadow from its caster (the long-standing "shadows don't work" bug).
    float2 uv = float2(shadowClip.x, -shadowClip.y) * 0.5f + 0.5f;
    if (any(uv < float2(0.0f, 0.0f)) || any(uv > float2(1.0f, 1.0f)) || shadowClip.z <= 0.0f || shadowClip.z >= 1.0f)
        return 1.0f;

    float2 atlasUv = cascadeXform.zw + uv * cascadeXform.xy;
    const float shadowSoftness = max(u_shadowParams0.w, 0.5f);
    const float cascadeScale = 1.0f + (float)cascade * 0.35f;
    const float receiverDistance = saturate(length(worldPos - u_cameraPos.xyz) / max(u_shadowParams1.z, 1.0f));
    const float2 texel = u_shadowParams0.zz * shadowSoftness * cascadeScale;
    const float compareDepth = shadowClip.z - (u_shadowParams1.x / depthRange) * slopeScale;

    static const float2 poisson[16] = {
        float2(-0.9420f, -0.3991f), float2( 0.9456f, -0.7689f),
        float2(-0.0942f, -0.9294f), float2( 0.3449f,  0.2939f),
        float2(-0.9159f,  0.4577f), float2(-0.8154f, -0.8791f),
        float2(-0.3828f,  0.2768f), float2( 0.9748f,  0.7565f),
        float2( 0.4432f, -0.9751f), float2( 0.5374f, -0.4737f),
        float2(-0.2649f, -0.4189f), float2( 0.7919f,  0.1909f),
        float2(-0.2419f,  0.9971f), float2(-0.8141f,  0.9144f),
        float2( 0.1998f,  0.7864f), float2( 0.1438f, -0.1410f)
    };

    float blockerDepth = 0.0f;
    float blockerCount = 0.0f;
    const float2 searchRadius = texel * (1.25f + shadowSoftness * 0.35f);
    [unroll]
    for (int blockerIndex = 0; blockerIndex < 8; ++blockerIndex)
    {
        const float sampleDepth = texShadowAtlas.Sample(sampLinear, atlasUv + poisson[blockerIndex] * searchRadius).r;
        const float blocked = compareDepth > sampleDepth ? 1.0f : 0.0f;
        blockerDepth += sampleDepth * blocked;
        blockerCount += blocked;
    }

    const float averageBlocker = blockerDepth / max(blockerCount, 1.0f);
    const float contactPenumbra = saturate((compareDepth - averageBlocker) * 85.0f);
    const float filterRadius = 1.0f
        + shadowSoftness * 0.72f
        + contactPenumbra * shadowSoftness * 2.15f
        + receiverDistance * shadowSoftness * 0.65f;

    float visibility = 0.0f;
    float totalWeight = 0.0f;
    [unroll]
    for (int sampleIndex = 0; sampleIndex < 16; ++sampleIndex)
    {
        const float2 offset = poisson[sampleIndex] * texel * filterRadius;
        const float sampleDepth = texShadowAtlas.Sample(sampLinear, atlasUv + offset).r;
        const float sampleVisibility = compareDepth <= sampleDepth ? 1.0f : 0.0f;
        const float weight = 1.0f - saturate(length(poisson[sampleIndex]) * 0.38f);
        visibility += sampleVisibility * weight;
        totalWeight += weight;
    }
    visibility /= max(totalWeight, 1e-4f);
    visibility = smoothstep(0.0f, 1.0f, visibility);

    return lerp(1.0f, visibility, saturate(u_shadowParams0.y));
}

float ProbeMask(float3 worldPos)
{
    if (u_probeCenter.w <= 0.0f || u_probeExtents.w <= 0.0f)
        return 0.0f;

    float3 local = abs(worldPos - u_probeCenter.xyz) / max(u_probeExtents.xyz, float3(0.001f, 0.001f, 0.001f));
    float edge = saturate(1.0f - max(local.x, max(local.y, local.z)));
    return edge * edge * u_probeExtents.w;
}

float2 ComputeVelocity(float4 currentClip, float4 previousClip)
{
    const float2 currentUv = currentClip.xy / max(abs(currentClip.w), 1e-5f) * 0.5f + 0.5f;
    const float2 previousUv = previousClip.xy / max(abs(previousClip.w), 1e-5f) * 0.5f + 0.5f;
    return currentUv - previousUv;
}

float3 SampleDDGI(float3 worldPos, float3 normal, float3 baseColor, float3 kD, float ao)
{
    if (u_ddgiParams.x < 0.5f)
        return 0.0f;

    const uint probeCount = min((uint)u_ddgiParams.y, 32u);
    if (probeCount == 0)
        return 0.0f;

    float3 irradiance = 0.0f;
    float weightSum = 0.0f;
    const float normalBias = max(u_ddgiParams.w, 0.0f);
    const float leakReduction = saturate(u_ddgiParams1.x);
    const float3 biasedPos = worldPos + normal * normalBias;

    [loop]
    for (uint probeIndex = 0; probeIndex < probeCount; ++probeIndex)
    {
        DDGIProbe probe = u_ddgiProbes[probeIndex];
        float3 toProbe = probe.positionRadius.xyz - biasedPos;
        float dist = length(toProbe);
        float radius = max(probe.positionRadius.w, 0.001f);
        float distanceWeight = saturate(1.0f - dist / radius);
        distanceWeight *= distanceWeight;

        float3 probeDir = dist > 1e-4f ? toProbe / dist : normal;
        float normalWeight = saturate(dot(normal, -probeDir) * 0.5f + 0.5f);

        float meanDistance = max(probe.visibility.x, radius * 0.45f);
        float variance = max(probe.visibility.y, radius * radius * 0.08f);
        float visibility = saturate((meanDistance + sqrt(variance) * (1.0f + leakReduction) - dist) / max(radius, 0.001f) + 0.5f);
        visibility = lerp(1.0f, visibility, leakReduction);

        float weight = distanceWeight * (0.25f + normalWeight * 0.75f) * visibility;
        irradiance += probe.irradiance.rgb * probe.irradiance.a * weight;
        weightSum += weight;
    }

    if (weightSum <= 1e-4f)
        return 0.0f;

    irradiance /= weightSum;
    return baseColor * kD * irradiance * u_ddgiParams.z * ao;
}

PSOut PSMain(PSIn input)
{
    float3 N = normalize(input.normal);
    if (u_flags.y > 0.5f && length(input.tangent.xyz) > 0.001f) {
        float3 T = normalize(input.tangent.xyz);
        float3 B = normalize(cross(N, T) * input.tangent.w);
        float3x3 TBN = float3x3(T, B, N);
        float3 tn = texNormal.Sample(sampLinear, input.uv).xyz * 2.0f - 1.0f;
        N = normalize(mul(tn, TBN));
    }

    float4 albedoSample = (u_flags.x > 0.5f) ? texAlbedo.Sample(sampLinear, input.uv)
                                             : float4(1.0f, 1.0f, 1.0f, 1.0f);
    float3 baseColor = saturate(input.color.rgb * u_albedo.rgb * albedoSample.rgb);
    float alpha = saturate(input.color.a * u_albedo.a * albedoSample.a);

    float roughness = saturate(u_params.x);
    float metallic = saturate(u_params.y);
    if (u_flags.z > 0.5f) {
        float3 mr = texMetallicRoughness.Sample(sampLinear, input.uv).rgb;
        roughness = saturate(mr.g);
        metallic = saturate(mr.b);
    }
    roughness = max(roughness, 0.045f);

    const float ao = saturate(u_params.w);
    float3 emissive = (u_flags.w > 0.5f) ? texEmissive.Sample(sampLinear, input.uv).rgb : 0.0f;

    const float3 V = normalize(u_cameraPos.xyz - input.worldPos);
    const float3 L = normalize(-u_lightDir.xyz);
    const float3 H = normalize(V + L);
    const float3 R = reflect(-V, N);

    const float NdotL = saturate(dot(N, L));
    const float NdotV = saturate(dot(N, V));
    const float HdotV = saturate(dot(H, V));

    const float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), baseColor, metallic);
    const float3 F = FresnelSchlick(HdotV, F0);
    const float D = DistributionGGX(N, H, roughness);
    const float G = GeometrySmith(N, V, L, roughness);

    // Pre-integrated split-sum term drives both the multi-scatter energy
    // compensation below and the image-based ambient specular later on.
    const float2 envAB = EnvBRDFApproxAB(roughness, NdotV);
    // Fdez-Aguera multi-scatter compensation: reintroduces the energy a
    // single-scatter GGX loses at high roughness so metals/rough dielectrics
    // keep their proper brightness instead of darkening.
    const float3 energyCompensation = 1.0f + F0 * (1.0f / max(envAB.y, 1e-3f) - 1.0f);
    const float3 specular = (D * G * F) / max(4.0f * NdotL * NdotV, 1e-4f) * energyCompensation;

    const float3 kS = F;
    const float3 kD = (1.0f - kS) * (1.0f - metallic);
    const float shadow = SampleShadow(input.worldPos, N, L);
    const float3 directDiffuse = kD * baseColor / PI;
    const float3 directLighting = (directDiffuse + specular) * u_lightColor.rgb * NdotL * shadow;

    float3 localLighting = 0.0f;
    const uint localLightCount = min((uint)u_localLightCount.x, 16u);
    [loop]
    for (uint lightIndex = 0; lightIndex < localLightCount; ++lightIndex)
    {
        LocalLight localLight = u_localLights[lightIndex];
        float3 toLight = localLight.positionRange.xyz - input.worldPos;
        float dist = length(toLight);
        float range = max(localLight.positionRange.w, 0.001f);
        if (dist <= range)
        {
            float3 localL = toLight / max(dist, 1e-4f);
            float rangeFade = saturate(1.0f - dist / range);
            float attenuation = (rangeFade * rangeFade) / max(1.0f, dist * dist * 0.025f);

            if (localLight.directionType.w > 1.5f)
            {
                float cone = dot(normalize(-localL), normalize(localLight.directionType.xyz));
                float innerCos = localLight.spotParams.x;
                float outerCos = min(localLight.spotParams.y, innerCos - 0.001f);
                float spotFade = saturate((cone - outerCos) / max(innerCos - outerCos, 0.001f));
                attenuation *= spotFade * spotFade;
            }

            float localNdotL = saturate(dot(N, localL));
            if (localNdotL > 0.0f && attenuation > 0.0001f)
            {
                float3 localH = normalize(V + localL);
                float localHdotV = saturate(dot(localH, V));
                float3 localF = FresnelSchlick(localHdotV, F0);
                float localD = DistributionGGX(N, localH, roughness);
                float localG = GeometrySmith(N, V, localL, roughness);
                float3 localSpecular = (localD * localG * localF) / max(4.0f * localNdotL * NdotV, 1e-4f);
                float3 localKS = localF;
                float3 localKD = (1.0f - localKS) * (1.0f - metallic);
                float3 radiance = localLight.colorIntensity.rgb * localLight.colorIntensity.a * attenuation;
                localLighting += (localKD * baseColor / PI + localSpecular) * radiance * localNdotL;
            }
        }
    }

    const float skyFacing = saturate(R.y * 0.5f + 0.5f);
    const float localProbe = ProbeMask(input.worldPos) * u_probeCenter.w * u_iblParams.w;
    const float3 iblColor = u_skyAmbient.rgb * (1.0f + localProbe);

    // Energy-conserving image-based ambient. The diffuse term uses a
    // roughness-aware Fresnel so that at grazing angles energy migrates from
    // diffuse into specular instead of double-counting.
    const float3 ambientF = FresnelSchlickRoughness(NdotV, F0, roughness);
    const float3 ambientKd = (1.0f - ambientF) * (1.0f - metallic);
    const float3 ambientDiffuse = baseColor * ambientKd * (u_ambient.rgb + iblColor * u_iblParams.x) * ao;

    // Split-sum specular: env colour * (F0 * scale + bias), modulated by a
    // horizon fade and a specular-occlusion factor derived from AO so cavities
    // don't glow. Reflections point up toward the brighter sky hemisphere.
    const float3 specularColor = F0 * envAB.x + envAB.y;
    const float horizon = saturate(1.0f + dot(R, N));
    const float specularOcclusion = saturate(pow(NdotV + ao, exp2(-16.0f * roughness - 1.0f)) - 1.0f + ao);
    const float3 ambientSpecular = specularColor
                                 * iblColor
                                 * u_iblParams.y
                                 * u_iblParams.z
                                 * (horizon * horizon)
                                 * lerp(0.5f, 1.0f, skyFacing)
                                 * specularOcclusion;

    float3 lit = directLighting + localLighting + ambientDiffuse + ambientSpecular + emissive;

    if (u_giParams.x > 0.5f)
    {
        const bool bakedGI = u_ddgiParams1.w > 0.5f;
        float3 ddgiDiffuse = SampleDDGI(input.worldPos, N, baseColor, kD, ao);
        float radiusMask = saturate(1.0f - length(u_cameraPos.xyz - input.worldPos) / max(u_giColor.w, 1.0f));
        float hemisphere = saturate(N.y * 0.5f + 0.5f);
        float wrap = saturate(dot(N, normalize(-u_lightDir.xyz)) * 0.35f + 0.65f);
        float3 bounceColor = lerp(u_giColor.rgb, u_giColor.rgb * baseColor, saturate(u_giParams.w));
        float bakedBoost = bakedGI ? 1.28f : 1.0f;
        float3 fallbackDiffuse = baseColor * kD * bounceColor * u_giParams.y * bakedBoost
            * (0.28f + hemisphere * 0.54f + wrap * 0.18f)
            * (bakedGI ? 1.0f : (0.45f + radiusMask * 0.55f))
            * ao;
        float3 giDiffuse = (!bakedGI && u_ddgiParams.x > 0.5f) ? ddgiDiffuse : fallbackDiffuse;
        float3 giSpecular = specularColor * iblColor * u_giParams.z
            * lerp(0.2f, bakedGI ? 0.72f : 1.0f, 1.0f - roughness);
        lit += giDiffuse + giSpecular;
    }

    if (u_params.z > 0.5f)
        lit = lerp(lit, float3(1.25f, 0.98f, 0.74f), 0.18f);

    if (u_fogParams.w > 0.5f && u_fogColorDensity.a > 0.0001f)
    {
        float dist = length(u_cameraPos.xyz - input.worldPos);
        float fogHeight = u_fogParams.x;
        float falloff = max(u_fogParams.y, 0.0001f);
        float startDist = u_fogParams.z;
        float heightAtten = exp(-max(input.worldPos.y - fogHeight, 0.0f) * falloff);
        float fogAmt = 1.0f - exp(-(u_fogColorDensity.a * 0.45f + u_atmosphereParams0.x * 0.015f)
                                  * max(dist - startDist, 0.0f)
                                  * heightAtten);
        fogAmt = saturate(fogAmt);

        float3 viewDir = normalize(input.worldPos - u_cameraPos.xyz);
        float sunScatter = pow(saturate(dot(viewDir, L)), 12.0f);
        float3 fogColor = u_fogColorDensity.rgb + u_lightColor.rgb * sunScatter * (0.25f + u_atmosphereParams1.x * 0.015f);
        lit = lerp(lit, fogColor, fogAmt * saturate(u_atmosphereParams1.w + 0.35f));
    }

    PSOut output;
    output.color = float4(max(lit, 0.0f), alpha);
    output.velocity = ComputeVelocity(input.currentClip, input.previousClip);
    return output;
}
