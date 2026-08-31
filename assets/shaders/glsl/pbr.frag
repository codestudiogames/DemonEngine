#version 450
// ==============================================================================
//  DemonEngine — PBR Fragment Shader  (Cook-Torrance GGX)
// ==============================================================================

layout(location = 0) in  vec3 fragPosition;
layout(location = 1) in  vec3 fragNormal;
layout(location = 2) in  vec2 fragTexCoord;
layout(location = 3) in  mat3 fragTBN;

layout(location = 0) out vec4 outColor;

// ── Set 0 — frame UBO ─────────────────────────────────────────────────────────
layout(set = 0, binding = 0) uniform FrameUBO {
    mat4  view;
    mat4  projection;
    mat4  viewProjection;
    vec3  cameraPosition;
    float time;
    vec2  resolution;
    float nearClip;
    float farClip;
} frame;

// ── Set 0 — lights ────────────────────────────────────────────────────────────
struct Light {
    vec4  positionAndType;   // xyz=pos, w=type (0=dir,1=point,2=spot)
    vec4  colorAndIntensity; // xyz=color, w=intensity
    vec4  directionAndRange; // xyz=dir, w=range
    vec4  spotAngles;        // x=inner cos, y=outer cos
};
layout(set = 0, binding = 1) uniform LightsUBO {
    Light lights[16];
    int   count;
} lightsBuffer;

// ── Set 1 — material textures ─────────────────────────────────────────────────
layout(set = 1, binding = 0) uniform sampler2D texAlbedo;
layout(set = 1, binding = 1) uniform sampler2D texNormal;
layout(set = 1, binding = 2) uniform sampler2D texMetallicRoughness; // R=metallic G=roughness
layout(set = 1, binding = 3) uniform sampler2D texAO;
layout(set = 1, binding = 4) uniform sampler2D texEmissive;

// ── Set 1 — material parameters ──────────────────────────────────────────────
layout(set = 1, binding = 5) uniform MaterialUBO {
    vec4  albedoFactor;
    float metallicFactor;
    float roughnessFactor;
    float occlusionStrength;
    float emissiveStrength;
    vec3  emissiveFactor;
    float alphaCutoff;
    int   hasNormalMap;
} mat;

// ── PBR Helpers ───────────────────────────────────────────────────────────────
const float PI = 3.14159265359;

float distributionGGX(vec3 N, vec3 H, float roughness) {
    float a   = roughness * roughness;
    float a2  = a * a;
    float NdH = max(dot(N, H), 0.0);
    float d   = (NdH * NdH * (a2 - 1.0) + 1.0);
    return a2 / (PI * d * d);
}

float geometrySchlickGGX(float NdV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdV / (NdV * (1.0 - k) + k);
}

float geometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    return geometrySchlickGGX(max(dot(N,V),0.0), roughness)
         * geometrySchlickGGX(max(dot(N,L),0.0), roughness);
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 computeLight(vec3 N, vec3 V, vec3 L, vec3 lightColor,
                  float attenuation, vec3 albedo,
                  float metallic, float roughness, vec3 F0)
{
    vec3  H   = normalize(V + L);
    float NdL = max(dot(N, L), 0.0);
    float NdV = max(dot(N, V), 0.0);

    float NDF = distributionGGX(N, H, roughness);
    float G   = geometrySmith(N, V, L, roughness);
    vec3  F   = fresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3  num   = NDF * G * F;
    float denom = 4.0 * NdV * NdL + 0.0001;
    vec3  spec  = num / denom;

    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
    return (kD * albedo / PI + spec) * lightColor * attenuation * NdL;
}

// ─────────────────────────────────────────────────────────────────────────────
void main() {
    // Sample textures
    vec4 albedoSample = texture(texAlbedo, fragTexCoord) * mat.albedoFactor;
    if (albedoSample.a < mat.alphaCutoff) discard;

    vec3  albedo    = pow(albedoSample.rgb, vec3(2.2)); // sRGB → linear
    vec2  mr        = texture(texMetallicRoughness, fragTexCoord).rg;
    float metallic  = mr.r * mat.metallicFactor;
    float roughness = mr.g * mat.roughnessFactor;
    float ao        = texture(texAO, fragTexCoord).r;
    vec3  emissive  = texture(texEmissive, fragTexCoord).rgb * mat.emissiveFactor * mat.emissiveStrength;

    // Normal mapping
    vec3 N = normalize(fragNormal);
    if (mat.hasNormalMap != 0) {
        vec3 tn = texture(texNormal, fragTexCoord).xyz * 2.0 - 1.0;
        N = normalize(fragTBN * tn);
    }

    vec3 V  = normalize(frame.cameraPosition - fragPosition);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    // Accumulate lights
    vec3 Lo = vec3(0.0);
    for (int i = 0; i < lightsBuffer.count; ++i) {
        Light lt = lightsBuffer.lights[i];
        int   tp = int(lt.positionAndType.w);
        vec3  lc = lt.colorAndIntensity.rgb * lt.colorAndIntensity.w;
        vec3  L;
        float att = 1.0;

        if (tp == 0) {
            // Directional
            L = normalize(-lt.directionAndRange.xyz);
        } else {
            // Point / Spot
            vec3  toLight = lt.positionAndType.xyz - fragPosition;
            float dist    = length(toLight);
            float range   = lt.directionAndRange.w;
            att  = clamp(1.0 - pow(dist / range, 4.0), 0.0, 1.0) / (dist * dist + 1.0);
            L    = normalize(toLight);

            if (tp == 2) {
                // Spot attenuation
                float theta   = dot(L, normalize(-lt.directionAndRange.xyz));
                float epsilon = lt.spotAngles.x - lt.spotAngles.y;
                att *= clamp((theta - lt.spotAngles.y) / epsilon, 0.0, 1.0);
            }
        }

        Lo += computeLight(N, V, L, lc, att, albedo, metallic, roughness, F0);
    }

    // Ambient (simple IBL approximation)
    vec3 ambient = vec3(0.03) * albedo * ao;
    vec3 colour  = ambient + Lo + emissive;

    // Tone mapping (ACES filmic) + gamma correction
    colour = colour / (colour + vec3(1.0));   // Reinhard; replace with ACES for quality
    colour = pow(colour, vec3(1.0 / 2.2));

    outColor = vec4(colour, albedoSample.a);
}
