cbuffer ShadowSceneCB : register(b0)
{
    float4x4 u_viewProj;
};

cbuffer ObjectCB : register(b1)
{
    float4x4 u_model;
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

float4 VSMain(VSIn input) : SV_POSITION
{
    float4 localPosition = float4(input.position, 1.0f);
    const bool skinned = (u_skinning.x > 0.5f) && (dot(input.boneWeights, 1.0f) > 0.0f);
    if (skinned) {
        float4 blendedPosition = 0.0f;
        [unroll]
        for (int influence = 0; influence < 4; ++influence) {
            const float weight = input.boneWeights[influence];
            if (weight <= 0.0f)
                continue;
            blendedPosition += mul(u_boneMatrices[min(input.boneIndices[influence], 255)], localPosition) * weight;
        }
        localPosition = blendedPosition;
    }

    float4 world = mul(u_model, localPosition);
    return mul(u_viewProj, world);
}
