cbuffer TileClassifyCB : register(b0)
{
    uint g_width;
    uint g_height;
    uint g_tileCountX;
    uint g_tileCountY;
};

Texture2D<float> g_depth : register(t0);
RWStructuredBuffer<uint> g_tileMask : register(u0);

groupshared uint s_occupied;

[numthreads(8, 8, 1)]
void CSMain(uint3 groupId : SV_GroupID, uint3 groupThreadId : SV_GroupThreadID)
{
    if (groupThreadId.x == 0 && groupThreadId.y == 0)
        s_occupied = 0;

    GroupMemoryBarrierWithGroupSync();

    const uint2 pixel = groupId.xy * 8 + groupThreadId.xy;
    if (pixel.x < g_width && pixel.y < g_height) {
        const float depth = g_depth.Load(int3(pixel, 0));
        if (depth > 0.0f && depth < 0.99999f)
            InterlockedOr(s_occupied, 1u);
    }

    GroupMemoryBarrierWithGroupSync();

    if (groupThreadId.x == 0 && groupThreadId.y == 0) {
        const uint tileIndex = groupId.y * g_tileCountX + groupId.x;
        if (groupId.x < g_tileCountX && groupId.y < g_tileCountY)
            g_tileMask[tileIndex] = s_occupied;
    }
}
