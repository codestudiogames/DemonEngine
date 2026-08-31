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

struct VSIn
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD0;
    float4 tangent  : TANGENT;
    float4 color    : COLOR0;
};

struct VSOut
{
    float4 pos          : SV_POSITION;
    float3 worldPos     : TEXCOORD0;
    float3 normal       : TEXCOORD1;
    float2 uv           : TEXCOORD2;
    float4 color        : COLOR0;
    float4 currentClip  : TEXCOORD3;
    float4 previousClip : TEXCOORD4;
    float  waveHeight   : TEXCOORD5; // 0..1 normalised crest height for PS depth tint
};

static const float PI2 = 6.28318530718f;

float2 ResolveFlowDirection()
{
    float2 direction = u_flags.xy;
    const float lenSq = dot(direction, direction);
    if (lenSq < 1e-6f)
        return float2(1.0f, 0.0f);
    return normalize(direction);
}

// -----------------------------------------------------------------------
// Gerstner wave: returns (deltaY, dNx, dNz) contribution for one octave.
//   dir      – normalised 2D wave direction
//   amp      – wave amplitude
//   wlen     – wavelength
//   speed    – angular speed (rad/s)
//   steepness– Q factor  (0 = sine, 1 = sharp crest)
// -----------------------------------------------------------------------
float3 GerstnerWave(float2 xz, float2 dir,
                    float amp, float wlen, float speed, float steepness,
                    float t)
{
    const float k  = PI2 / max(wlen, 0.01f);
    const float c  = sqrt(9.8f / k);          // physically based phase velocity
    const float w  = k * c * speed;
    const float Q  = steepness / max(k * amp, 1e-5f);
    const float f  = k * dot(dir, xz) - w * t;
    const float cf = cos(f);
    const float sf = sin(f);

    float deltaY = amp * sf;
    // Surface normal contribution  (analytic partial derivatives)
    float dNx = -k * amp * cf * dir.x;
    float dNz = -k * amp * cf * dir.y;
    return float3(deltaY, dNx, dNz);
}

// -----------------------------------------------------------------------
// Four-octave wave stack.  Directions spread ~45° apart so waves cross
// and produce the choppy appearance seen in the reference image.
// -----------------------------------------------------------------------
float SampleWaveHeight(float2 xz, float t)
{
    const float  amp   = u_params.x;
    const float  wlen  = max(u_params.y, 0.1f);
    const float  speed = u_params.z;

    if (abs(amp) < 1e-4f) return 0.0f;

    float2 d0 = ResolveFlowDirection();
    float2 d1 = normalize(float2( d0.x + d0.y,  d0.y - d0.x));  // +45°
    float2 d2 = normalize(float2(-d0.y,          d0.x));          // +90°
    float2 d3 = normalize(float2( d0.x - d0.y,  d0.y + d0.x));  // -45°

    float h = 0.0f;
    h += GerstnerWave(xz, d0, amp,          wlen,          speed,        0.5f, t).x;
    h += GerstnerWave(xz, d1, amp * 0.55f,  wlen * 0.62f,  speed * 1.15f, 0.4f, t).x;
    h += GerstnerWave(xz, d2, amp * 0.35f,  wlen * 0.40f,  speed * 1.30f, 0.3f, t).x;
    h += GerstnerWave(xz, d3, amp * 0.20f,  wlen * 0.28f,  speed * 1.60f, 0.25f, t).x;
    return h;
}

float3 SampleWaveNormal(float2 xz, float t)
{
    const float  amp   = u_params.x;
    const float  wlen  = max(u_params.y, 0.1f);
    const float  speed = u_params.z;

    if (abs(amp) < 1e-4f) return float3(0.0f, 1.0f, 0.0f);

    float2 d0 = ResolveFlowDirection();
    float2 d1 = normalize(float2( d0.x + d0.y,  d0.y - d0.x));
    float2 d2 = normalize(float2(-d0.y,          d0.x));
    float2 d3 = normalize(float2( d0.x - d0.y,  d0.y + d0.x));

    // Accumulate analytic normal deltas (x,z) from each octave
    float2 dn = float2(0.0f, 0.0f);
    float3 g;
    g = GerstnerWave(xz, d0, amp,          wlen,          speed,        0.5f,  t); dn += g.yz;
    g = GerstnerWave(xz, d1, amp * 0.55f,  wlen * 0.62f,  speed * 1.15f, 0.4f, t); dn += g.yz;
    g = GerstnerWave(xz, d2, amp * 0.35f,  wlen * 0.40f,  speed * 1.30f, 0.3f, t); dn += g.yz;
    g = GerstnerWave(xz, d3, amp * 0.20f,  wlen * 0.28f,  speed * 1.60f, 0.25f,t); dn += g.yz;

    // Reconstruct world-space normal from tangent-space deltas
    float3 normal = normalize(float3(-dn.x, 1.0f, -dn.y));
    return normal;
}

VSOut VSMain(VSIn input)
{
    VSOut o;
    float3 localPosition = input.position;
    float3 localNormal   = input.normal;
    const float t = u_cameraPos.w;

    float waveH = 0.0f;
    if (input.position.y > -0.001f)
    {
        waveH            = SampleWaveHeight(input.position.xz, t);
        localPosition.y += waveH;
        localNormal      = SampleWaveNormal(input.position.xz, t);
    }

    const float4 world     = mul(u_model,     float4(localPosition, 1.0f));
    const float4 prevWorld = mul(u_prevModel, float4(localPosition, 1.0f));
    o.currentClip  = mul(u_viewProj,         world);
    o.previousClip = mul(u_previousViewProj, prevWorld);
    o.pos          = o.currentClip;
    o.worldPos     = world.xyz;
    o.normal       = mul((float3x3)u_model, localNormal);
    o.uv           = input.uv;
    o.color        = input.color;

    // Normalise crest height to [0,1] so PS can use it for depth tinting.
    // waveH is in metres; divide by total max possible amplitude (~2×amp).
    const float maxAmp = u_params.x * 2.1f;
    o.waveHeight = (maxAmp > 1e-4f) ? saturate((waveH + maxAmp) / (2.0f * maxAmp)) : 0.5f;

    return o;
}
