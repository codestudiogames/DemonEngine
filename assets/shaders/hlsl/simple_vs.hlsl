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

cbuffer BonesCB : register(b2)
{
    float4x4 u_boneMatrices[256];
};

struct VSIn
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD0;
    float4 tangent  : TANGENT;
    float4 color    : COLOR0;
    uint4  boneIndices : BLENDINDICES;
    float4 boneWeights : BLENDWEIGHT;
};

struct VSOut
{
    float4 pos          : SV_POSITION;
    float3 worldPos     : TEXCOORD0;
    float3 normal       : TEXCOORD1;
    float2 uv           : TEXCOORD2;
    float4 tangent      : TEXCOORD3;
    float4 color        : COLOR0;
    float4 currentClip  : TEXCOORD4;
    float4 previousClip : TEXCOORD5;
};

VSOut VSMain(VSIn input)
{
    VSOut o;
    float4 localPosition = float4(input.position, 1.0f);
    float3 localNormal = input.normal;
    float3 localTangent = input.tangent.xyz;

    const bool skinned = (u_skinning.x > 0.5f) && (dot(input.boneWeights, 1.0f) > 0.0f);
    if (skinned) {
        float4 blendedPosition = 0.0f;
        float3 blendedNormal = 0.0f;
        float3 blendedTangent = 0.0f;
        [unroll]
        for (int influence = 0; influence < 4; ++influence) {
            const float weight = input.boneWeights[influence];
            if (weight <= 0.0f)
                continue;
            const float4x4 boneMatrix = u_boneMatrices[min(input.boneIndices[influence], 255)];
            blendedPosition += mul(boneMatrix, localPosition) * weight;
            blendedNormal += mul((float3x3)boneMatrix, input.normal) * weight;
            blendedTangent += mul((float3x3)boneMatrix, input.tangent.xyz) * weight;
        }
        localPosition = blendedPosition;
        localNormal = normalize(blendedNormal);
        localTangent = normalize(blendedTangent);
    }

    float4 world = mul(u_model, localPosition);
    float4 prevWorld = mul(u_prevModel, localPosition);
    o.currentClip = mul(u_viewProj, world);
    o.previousClip = mul(u_previousViewProj, prevWorld);
    o.pos = o.currentClip;
    o.worldPos = world.xyz;
    o.normal = mul((float3x3)u_model, localNormal);
    o.uv = input.uv;
    o.tangent = float4(mul((float3x3)u_model, localTangent), input.tangent.w);
    o.color = input.color;
    return o;
}
