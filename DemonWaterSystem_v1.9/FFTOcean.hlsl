// =============================================================================
// FFTOcean.hlsl  –  DemonEngine Water Simulation
// GPU Cooley-Tukey FFT ocean using Phillips spectrum (JONSWAP variant)
// =============================================================================

// ---- Constant buffer (b0) ---------------------------------------------------
cbuffer FFTOceanCB : register(b0)
{
    float  g_Time;
    float  g_DeltaTime;
    float  g_PatchSize;
    float  g_WindSpeed;
    float  g_WindDirX;
    float  g_WindDirY;
    float  g_Fetch;
    float  g_Choppiness;
    float  g_WaveHeightScale;
    int    g_N;
    int    g_Log2N;
    float  g_Gravity;
    float  g_TimeScale;
    float  _pad[3];
}

// ---- Resources --------------------------------------------------------------
Texture2D<float2>   t_H0         : register(t0);   // H0(k) initial spectrum
Texture2D<float4>   t_Butterfly  : register(t1);   // butterfly lookup
Texture2D<float2>   t_SlopeX     : register(t2);   // horizontal slope
Texture2D<float2>   t_SlopeZ     : register(t3);   // vertical slope
Texture2D<float2>   t_HtDx       : register(t4);
Texture2D<float2>   t_HtDy       : register(t5);
Texture2D<float2>   t_HtDz       : register(t6);
Texture2D<float4>   t_Displacement: register(t7);  // packed displacement

RWTexture2D<float2> u_H0         : register(u0);
RWTexture2D<float2> u_HtDx       : register(u1);
RWTexture2D<float2> u_HtDy       : register(u2);
RWTexture2D<float2> u_HtDz       : register(u3);
RWTexture2D<float2> u_SlopeX     : register(u4);
RWTexture2D<float2> u_SlopeZ     : register(u5);
RWTexture2D<float4> u_Displacement: register(u6);
RWTexture2D<float4> u_Normal      : register(u7);
RWTexture2D<float>  u_Foam        : register(u8);

// ---- Math helpers -----------------------------------------------------------
static const float PI  = 3.14159265359f;
static const float TWO_PI = 6.28318530718f;

float2 ComplexMul(float2 a, float2 b)
{
    return float2(a.x*b.x - a.y*b.y, a.x*b.y + a.y*b.x);
}
float2 ComplexConj(float2 a)
{
    return float2(a.x, -a.y);
}

// ---- Phillips Spectrum  H0(k) -----------------------------------------------
// Uses JONSWAP peak sharpening + directional spread
float PhillipsSpectrum(float2 k)
{
    float kLen = length(k);
    if (kLen < 0.0001f) return 0.f;

    // Peak wave number for JONSWAP
    float kp  = g_Gravity / (g_WindSpeed * g_WindSpeed);

    // Directional cosine (khat · wdir)
    float2 wdir  = normalize(float2(g_WindDirX, g_WindDirY));
    float2 khat  = k / kLen;
    float  costh = dot(khat, wdir);
    if (costh < 0.f) return 0.f;   // waves only travel into wind

    // Phillips formula
    float L  = g_WindSpeed * g_WindSpeed / g_Gravity;
    float A  = 1.f;                 // amplitude constant (tuned via WaveHeight)
    float kL = kLen * L;
    float Ph = A * exp(-1.f / (kL * kL)) / (kLen * kLen * kLen * kLen);

    // Suppress very small waves (removes aliasing ripple)
    float l  = L * 0.001f;
    Ph *= exp(-kLen * kLen * l * l);

    // Directional spread: cos^2 (Phillips), cos^6 (JONSWAP-like)
    float dirSpread = costh * costh * costh * costh * costh * costh;
    Ph *= dirSpread;

    return Ph * g_WaveHeightScale * g_WaveHeightScale;
}

// Box-Muller Gaussian noise (deterministic from pixel coords)
float2 GaussianRandom(uint2 id, uint seed)
{
    // Hash to uniform [0,1)
    uint h  = id.x + id.y * 1973u + seed * 9277u;
    h = (h ^ 61u) ^ (h >> 16u);
    h = h + (h << 3u);
    h = h ^ (h >> 4u);
    h = h * 0x27d4eb2du;
    h = h ^ (h >> 15u);
    float u1 = (float)(h & 0xFFFFFFu) / 16777216.f + 1e-7f;
    float u2 = (float)((h >> 8u) & 0xFFFFFFu) / 16777216.f + 1e-7f;
    float r  = sqrt(-2.f * log(u1));
    float th = TWO_PI * u2;
    return float2(r * cos(th), r * sin(th));
}

// =============================================================================
// CS_InitSpectrum  –  build H0(k) once per wind-change
// =============================================================================
[numthreads(8, 8, 1)]
void CS_InitSpectrum(uint3 tid : SV_DispatchThreadID)
{
    int2  id  = (int2)tid.xy;
    int   N   = g_N;
    float N2  = (float)(N / 2);

    // Wave-vector k in world space
    float2 k;
    k.x = (id.x - N2) * (TWO_PI / g_PatchSize);
    k.y = (id.y - N2) * (TWO_PI / g_PatchSize);

    float Ph = PhillipsSpectrum(k);
    float2 xi = GaussianRandom(tid.xy, 0u);

    // H0(k) = (xi_r + i*xi_i) / sqrt(2) * sqrt(Ph)
    float amp = sqrt(Ph * 0.5f);
    u_H0[id] = xi * amp;
}

// =============================================================================
// CS_UpdateSpectrum  –  animate H(k,t) from H0
// =============================================================================
[numthreads(8, 8, 1)]
void CS_UpdateSpectrum(uint3 tid : SV_DispatchThreadID)
{
    int2  id  = (int2)tid.xy;
    int   N   = g_N;
    float N2f = (float)(N / 2);
    float t   = g_Time * g_TimeScale;

    float2 k;
    k.x = (id.x - N2f) * (TWO_PI / g_PatchSize);
    k.y = (id.y - N2f) * (TWO_PI / g_PatchSize);

    float kLen = length(k);
    float w    = sqrt(g_Gravity * max(kLen, 0.0001f));  // deep-water dispersion

    // H(k,t) = H0(k)*e^(iwt) + conj(H0(-k))*e^(-iwt)
    float2 h0k    = t_H0[id];
    int2   minusK = int2((N - id.x) % N, (N - id.y) % N);
    float2 h0mink = t_H0[minusK];

    float cosW = cos(w * t), sinW = sin(w * t);
    float2 eit  = float2(cosW,  sinW);
    float2 emit = float2(cosW, -sinW);

    float2 ht = ComplexMul(h0k, eit) + ComplexMul(ComplexConj(h0mink), emit);

    // Displacement (Dx, Dz) uses ik/|k| trick for Longuet-Higgins choppiness
    float2 kn = (kLen > 0.0001f) ? k / kLen : float2(0,0);

    u_HtDy[id]  = ht;                                           // vertical
    u_HtDx[id]  = ComplexMul(float2(-kn.x, 0), ht) * g_Choppiness;  // lateral X
    u_HtDz[id]  = ComplexMul(float2(-kn.y, 0), ht) * g_Choppiness;  // lateral Z
    u_SlopeX[id]= ComplexMul(float2(0, k.x), ht);
    u_SlopeZ[id]= ComplexMul(float2(0, k.y), ht);
}

// =============================================================================
// FFT butterfly pass  (horizontal or vertical, controlled by permutation)
// =============================================================================
[numthreads(8, 8, 1)]
void CS_FFTHorizontal(uint3 tid : SV_DispatchThreadID)
{
    int2  id    = (int2)tid.xy;
    int   stage = 0; // pushed via root constants in real usage; here uses CB
    float4 bfly = t_Butterfly[int2(stage, id.x)];

    float2 twiddle  = bfly.xy;
    int2   topIdx   = int2((int)bfly.z, id.y);
    int2   botIdx   = int2((int)bfly.w, id.y);

    // Butterfly on Dy (caller dispatches for each texture)
    float2 a = t_HtDy[topIdx];
    float2 b = ComplexMul(twiddle, t_HtDy[botIdx]);
    u_HtDy[id] = a + b;
}

[numthreads(8, 8, 1)]
void CS_FFTVertical(uint3 tid : SV_DispatchThreadID)
{
    int2  id    = (int2)tid.xy;
    int   stage = 0;
    float4 bfly = t_Butterfly[int2(stage, id.y)];

    float2 twiddle = bfly.xy;
    int2   topIdx  = int2(id.x, (int)bfly.z);
    int2   botIdx  = int2(id.x, (int)bfly.w);

    float2 a = t_HtDy[topIdx];
    float2 b = ComplexMul(twiddle, t_HtDy[botIdx]);
    u_HtDy[id] = a + b;
}

// =============================================================================
// CS_PermutePack  –  apply sign permutation + pack into displacement / foam
// =============================================================================
[numthreads(8, 8, 1)]
void CS_PermutePack(uint3 tid : SV_DispatchThreadID)
{
    int2  id = (int2)tid.xy;
    int   N  = g_N;

    // Permutation sign: (-1)^(x+y)
    float perm = ((id.x + id.y) & 1) ? -1.f : 1.f;

    float dx = t_HtDx[id].x * perm;
    float dy = t_HtDy[id].x * perm;
    float dz = t_HtDz[id].x * perm;

    // Jacobian from displacement gradient → foam mask
    // J = (1 + dDx/dx)(1 + dDz/dz) - (dDx/dz)(dDz/dx)
    int2  ip1  = int2((id.x + 1) % N, id.y);
    int2  jp1  = int2(id.x, (id.y + 1) % N);
    float dxdx = (t_HtDx[ip1].x - dx) * perm;
    float dzdz = (t_HtDz[jp1].x - dz) * perm;
    float jacobian = (1.f + dxdx) * (1.f + dzdz);  // simplified

    u_Displacement[id] = float4(dx, dy, dz, 0.f);
    u_Foam[id]         = saturate(1.f - jacobian);   // >0 where waves break
}

// =============================================================================
// CS_NormalGen  –  Sobel on displacement Y to get world-space normals
// =============================================================================
[numthreads(8, 8, 1)]
void CS_NormalGen(uint3 tid : SV_DispatchThreadID)
{
    int2  id = (int2)tid.xy;
    int   N  = g_N;
    float texelSize = g_PatchSize / (float)N;

    // Sample 3x3 neighbourhood
    auto S = [&](int dx, int dz) -> float {
        int2 s = int2((id.x + dx + N) % N, (id.y + dz + N) % N);
        return t_Displacement[s].y;
    };

    // Sobel
    float dydx = (S(1,-1) + 2*S(1,0) + S(1,1) - S(-1,-1) - 2*S(-1,0) - S(-1,1))
                 / (8.f * texelSize);
    float dydz = (S(-1,1) + 2*S(0,1) + S(1,1) - S(-1,-1) - 2*S(0,-1) - S(1,-1))
                 / (8.f * texelSize);

    float3 n = normalize(float3(-dydx, 1.f, -dydz));

    u_Normal[id] = float4(n * 0.5f + 0.5f, 1.f);  // pack to [0,1]
}
