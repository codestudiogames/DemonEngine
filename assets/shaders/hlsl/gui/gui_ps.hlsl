// =============================================================================
//  DemonGUI  —  GUI Pixel Shader
//  Slot t0: colour texture OR font atlas (RGBA8, alpha in .a channel)
//  Slot s0: linear clamp sampler
// =============================================================================

Texture2D    tex     : register(t0);
SamplerState samp    : register(s0);

struct PSIn {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD;
    float4 col : COLOR;
};

float4 main(PSIn p) : SV_TARGET
{
    float4 s = tex.Sample(samp, p.uv);

    // Font glyphs: treat texture alpha directly.
    // Images: use full RGBA.
    // Both cases handled by multiplying vertex colour by sample.
    // The vertex colour encodes the tint; for font glyphs:
    //   rgb = text colour, alpha = 1 (glyph coverage comes from s.a)
    // For images:
    //   rgba = tint * sample

    float4 result = p.col * s;
    return result;
}
