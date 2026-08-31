#ifndef DEMON_POST_COMMON
#define DEMON_POST_COMMON

struct VSOut {
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

Texture2D    g_ColorTex : register(t0);
Texture2D    g_DepthTex : register(t1);
Texture2D    g_HistoryTex : register(t2);
Texture2D    g_AuxTex : register(t3);
SamplerState g_Sampler  : register(s0);

cbuffer PostParams : register(b0)
{
    float4 params0;
    float4 params1;
    float4 params2;
    float4 params3;
    float4x4 matrix0;
    float4x4 matrix1;
    float4 frame0;
    float4 frame1;
    float4 frame2;
};

float2 InvTexSize() { return params3.xy; }

float3 SampleColor(float2 uv) { return g_ColorTex.Sample(g_Sampler, uv).rgb; }
float  SampleDepth(float2 uv) { return g_DepthTex.Sample(g_Sampler, uv).r; }
float3 SampleHistory(float2 uv) { return g_HistoryTex.Sample(g_Sampler, uv).rgb; }
float2 SampleAux(float2 uv) { return g_AuxTex.Sample(g_Sampler, uv).rg; }

#endif
