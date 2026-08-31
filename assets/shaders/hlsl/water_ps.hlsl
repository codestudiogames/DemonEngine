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
    float4   u_params; // x = waveAmplitude, y = waveLength, z = waveSpeed, w = flowSpeed
    float4   u_flags;  // x = flowDirX, y = flowDirY, z = highlight, w = unused
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
    float  waveHeight   : TEXCOORD5;
};

struct PSOut
{
    float4 color    : SV_Target0;
    float2 velocity : SV_Target1;
};

float2 ComputeVelocity(float4 currentClip, float4 previousClip)
{
    const float2 currentUv  = currentClip.xy  / max(abs(currentClip.w),  1e-5f) * 0.5f + 0.5f;
    const float2 previousUv = previousClip.xy / max(abs(previousClip.w), 1e-5f) * 0.5f + 0.5f;
    return currentUv - previousUv;
}

// GGX / Trowbridge-Reitz NDF — gives physically wide specular lobe
float GGX_D(float NdotH, float roughness)
{
    const float a  = roughness * roughness;
    const float a2 = a * a;
    const float d  = (NdotH * NdotH) * (a2 - 1.0f) + 1.0f;
    return a2 / (3.14159265f * d * d + 1e-7f);
}

// Schlick fresnel
float Fresnel(float cosTheta, float F0)
{
    return F0 + (1.0f - F0) * pow(saturate(1.0f - cosTheta), 5.0f);
}

PSOut PSMain(PSIn input)
{
    float3 N = normalize(input.normal);
    float3 L = normalize(-u_lightDir.xyz);
    float3 V = normalize(u_cameraPos.xyz - input.worldPos);
    float3 H = normalize(L + V);

    const float NdotL = saturate(dot(N, L));
    const float NdotV = saturate(dot(N, V));
    const float NdotH = saturate(dot(N, H));

    // -------------------------------------------------------------------
    // TRUE water colours from the reference image
    // Deep teal  : #0a3d40   → (0.039, 0.239, 0.251)
    // Mid  teal  : #0e6b72   → (0.055, 0.420, 0.447)
    // Crest cyan : #38c0c8   → (0.220, 0.753, 0.784)
    // -------------------------------------------------------------------
    const float3 colDeep   = float3(0.039f, 0.239f, 0.251f);
    const float3 colMid    = float3(0.055f, 0.420f, 0.447f);
    const float3 colCrest  = float3(0.220f, 0.753f, 0.784f);

    // Blend through deep → mid → crest using the crest height from VS
    float3 waterColor = lerp(colDeep, colMid,   saturate(input.waveHeight * 1.8f));
    waterColor        = lerp(waterColor, colCrest, saturate((input.waveHeight - 0.55f) * 2.5f));

    // Tint by vertex colour (artist/CPU-set tint stays respected)
    waterColor *= input.color.rgb;

    // -------------------------------------------------------------------
    // Lighting
    // -------------------------------------------------------------------
    // Diffuse: subtle — water mostly reflects, not diffuses
    float3 diffuse = waterColor * (u_ambient.rgb + u_skyAmbient.rgb * 0.8f
                                   + NdotL * u_lightColor.rgb * 0.22f);

    // GGX specular — roughness ~0.08 for glassy choppy water
    const float roughness = 0.08f;
    const float D         = GGX_D(NdotH, roughness);
    const float F         = Fresnel(NdotV, 0.04f);
    const float specPower = D * F;
    float3 specular       = specPower * u_lightColor.rgb * 1.8f;

    // Secondary scattered specular lobe (broader, gives the glow across
    // the whole surface like the reference image)
    const float scatterSpec = pow(saturate(NdotH), 8.0f) * 0.18f;
    specular += scatterSpec * u_lightColor.rgb * float3(0.9f, 1.0f, 1.0f);

    // Fresnel-based sky reflection tint on grazing angles
    const float fresnelGraze = Fresnel(NdotV, 0.04f);
    const float3 skyRefl     = float3(0.52f, 0.78f, 0.85f);
    float3 lit = diffuse + specular + fresnelGraze * skyRefl * 0.45f;

    // Highlight (shoreline / editor selection)
    if (u_flags.z > 0.5f)
        lit = lerp(lit, float3(0.95f, 0.85f, 0.65f), 0.22f);

    // -------------------------------------------------------------------
    // Fog
    // -------------------------------------------------------------------
    if (u_fogParams.w > 0.5f && u_fogColorDensity.a > 0.0001f)
    {
        const float dist       = length(u_cameraPos.xyz - input.worldPos);
        const float fogHeight  = u_fogParams.x;
        const float falloff    = max(u_fogParams.y, 0.0001f);
        const float startDist  = u_fogParams.z;
        const float heightAtt  = exp(-max(input.worldPos.y - fogHeight, 0.0f) * falloff);
        float fogAmt = 1.0f - exp(-u_fogColorDensity.a
                                   * max(dist - startDist, 0.0f)
                                   * heightAtt);
        fogAmt = saturate(fogAmt);
        lit = lerp(lit, u_fogColorDensity.rgb, fogAmt);
    }

    // -------------------------------------------------------------------
    // Alpha — edge transparency like the reference image.
    //
    // The reference shows the water mesh fading to transparent at its
    // outer boundary (corners/edges visible as clear water over terrain).
    // We achieve this with two factors:
    //
    //   1) fresnelAlpha  — water is nearly transparent when seen straight
    //      down (grazing angle = transparent, glancing = opaque).
    //      This matches how real water looks at shallow incidence.
    //
    //   2) edgeFade      — uses the UV distance from mesh centre to fade
    //      out the perimeter.  UVs are assumed 0..1 across the mesh.
    // -------------------------------------------------------------------

    // Fresnel-based opacity: fully opaque at grazing angles, semi-transparent overhead
    const float fresnelAlpha = lerp(0.55f, 0.98f, fresnelGraze);

    // Edge fade: smooth falloff in the outer 20% of UV space
    const float2 uvCentered = abs(input.uv - 0.5f) * 2.0f; // 0 at centre, 1 at edge
    const float  edgeDist   = max(uvCentered.x, uvCentered.y);
    const float  edgeFade   = 1.0f - smoothstep(0.75f, 1.0f, edgeDist);

    // Wave-crest boosts opacity slightly (crests are more opaque)
    const float crestBoost  = lerp(0.0f, 0.12f, saturate(input.waveHeight * 2.0f - 0.8f));

    float alpha = saturate(input.color.a * u_albedo.a * fresnelAlpha * edgeFade + crestBoost);

    PSOut output;
    output.color    = float4(lit, alpha);
    output.velocity = ComputeVelocity(input.currentClip, input.previousClip);
    return output;
}
