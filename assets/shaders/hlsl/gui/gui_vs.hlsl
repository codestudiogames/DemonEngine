// =============================================================================
//  DemonGUI  —  GUI Vertex Shader
//  Root constant slot b0: float4 proj (scaleX, scaleY, biasX, biasY)
// =============================================================================

cbuffer ProjCB : register(b0)
{
    float2 Scale;
    float2 Bias;
};

struct VSIn {
    float2 pos   : POSITION;
    float2 uv    : TEXCOORD;
    float4 col   : COLOR;      // R8G8B8A8_UNORM auto-converted to float4 by IA
};

struct VSOut {
    float4 pos   : SV_POSITION;
    float2 uv    : TEXCOORD;
    float4 col   : COLOR;
};

VSOut main(VSIn v)
{
    VSOut o;
    o.pos = float4(v.pos * Scale + Bias, 0.0, 1.0);
    o.uv  = v.uv;
    o.col = v.col;
    return o;
}
