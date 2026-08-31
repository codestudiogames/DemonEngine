#version 450

layout(set = 0, binding = 0) uniform SceneUBO {
    mat4 u_viewProj;
    vec4 u_lightDir;
    vec4 u_lightColor;
    vec4 u_ambient;
} ubo;

layout(set = 1, binding = 0) uniform sampler2D u_albedoTex;
layout(set = 1, binding = 1) uniform sampler2D u_normalTex;
layout(set = 1, binding = 2) uniform sampler2D u_metallicRoughnessTex;
layout(set = 1, binding = 3) uniform sampler2D u_emissiveTex;

layout(push_constant) uniform Push {
    mat4 u_model;
    vec4 u_albedo;
    vec4 u_params;
    vec4 u_flags;
} pc;

layout(location = 0) in vec4 vColor;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec3 vWorldPos;
layout(location = 3) in vec2 vTexCoord;
layout(location = 4) in vec4 vTangent;

layout(location = 0) out vec4 outColor;

float gridLine(vec2 coord, float scale) {
    vec2 c = coord / scale;
    vec2 g = abs(fract(c - 0.5) - 0.5) / fwidth(c);
    return 1.0 - min(min(g.x, g.y), 1.0);
}

void main() {
    // Grid plane if alpha < 0.5
    if (vColor.a < 0.5) {
        float minor = gridLine(vWorldPos.xz, 1.0);
        float major = gridLine(vWorldPos.xz, 5.0);
        vec3 base   = vec3(0.08);
        vec3 minorC = vec3(0.25);
        vec3 majorC = vec3(0.45);
        vec3 color  = mix(base, minorC, minor * 0.7);
        color       = mix(color, majorC, major);
        outColor    = vec4(color, 1.0);
        return;
    }

    // Simple Lambert lighting
    vec3 N = normalize(vNormal);
    if (pc.u_flags.y > 0.5 && length(vTangent.xyz) > 0.001) {
        vec3 T = normalize(vTangent.xyz);
        vec3 B = normalize(cross(N, T) * vTangent.w);
        mat3 TBN = mat3(T, B, N);
        vec3 nrm = texture(u_normalTex, vTexCoord).rgb * 2.0 - 1.0;
        N = normalize(TBN * nrm);
    }
    vec3 L = normalize(-ubo.u_lightDir.xyz);
    float diff = max(dot(N, L), 0.0);
    vec3 base = vColor.rgb * pc.u_albedo.rgb;
    if (pc.u_flags.x > 0.5) {
        vec4 tex = texture(u_albedoTex, vTexCoord);
        base *= tex.rgb;
    }

    float rough = pc.u_params.x;
    float metal = pc.u_params.y;
    if (pc.u_flags.z > 0.5) {
        vec3 mr = texture(u_metallicRoughnessTex, vTexCoord).rgb;
        rough = mr.g;
        metal = mr.b;
    }

    vec3 lit  = base * (ubo.u_ambient.rgb + diff * ubo.u_lightColor.rgb);
    // Very simple metallic/roughness influence (non-PBR, but visible)
    lit = mix(lit, base * 0.04, metal);
    lit *= mix(1.0, 0.7, rough);
    lit *= pc.u_params.w; // AO

    if (pc.u_flags.w > 0.5) {
        vec3 emissive = texture(u_emissiveTex, vTexCoord).rgb;
        lit += emissive;
    }

    // Subtle highlight for selected entities
    if (pc.u_params.z > 0.5)
        lit = mix(lit, vec3(1.0, 0.85, 0.7), 0.25);

    outColor = vec4(lit, 1.0);
}
