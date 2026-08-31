cbuffer TileClassifyCB : register(b0)
{
    uint g_width;
    uint g_height;
    uint g_tileCountX;
    uint g_tileCountY;
};

Texture2D<float4> g_hdrColor : register(t0);
RWStructuredBuffer<uint> g_luminanceTiles : register(u0);

groupshared uint s_luminanceQ;
groupshared uint s_sampleCount;

float Luminance(float3 color)
{
    return dot(max(color, 0.0f), float3(0.2126f, 0.7152f, 0.0722f));
}

[numthreads(8, 8, 1)]
void CSMain(uint3 groupId : SV_GroupID, uint3 groupThreadId : SV_GroupThreadID)
{
    if (groupThreadId.x == 0 && groupThreadId.y == 0) {
        s_luminanceQ = 0u;
        s_sampleCount = 0u;
    }

    GroupMemoryBarrierWithGroupSync();

    const uint2 pixel = groupId.xy * 8 + groupThreadId.xy;
    if (pixel.x < g_width && pixel.y < g_height) {
        const float3 hdr = g_hdrColor.Load(int3(pixel, 0)).rgb;
        const float luma = min(Luminance(hdr), 64.0f);
        InterlockedAdd(s_luminanceQ, (uint)round(luma * 1024.0f));
        InterlockedAdd(s_sampleCount, 1u);
    }

    GroupMemoryBarrierWithGroupSync();

    if (groupThreadId.x == 0 && groupThreadId.y == 0 &&
        groupId.x < g_tileCountX && groupId.y < g_tileCountY)
    {
        const uint tileIndex = groupId.y * g_tileCountX + groupId.x;
        const uint count = max(s_sampleCount, 1u);
        g_luminanceTiles[tileIndex] = s_luminanceQ / count;
    }
}
