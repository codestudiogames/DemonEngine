#version 450
// ==============================================================================
//  DemonEngine — Standard PBR Vertex Shader
// ==============================================================================

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inTangent;

layout(location = 0) out vec3 fragPosition;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) out mat3 fragTBN;

// ── Push constants (per-draw model matrix) ────────────────────────────────────
layout(push_constant) uniform PushConstants {
    mat4 model;
} pc;

// ── Set 0 — per-frame global UBO ──────────────────────────────────────────────
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

// ─────────────────────────────────────────────────────────────────────────────
void main() {
    vec4 worldPos = pc.model * vec4(inPosition, 1.0);
    fragPosition  = worldPos.xyz;

    mat3 normalMatrix = transpose(inverse(mat3(pc.model)));
    vec3 N = normalize(normalMatrix * inNormal);
    vec3 T = normalize(normalMatrix * inTangent.xyz);
    T = normalize(T - dot(T, N) * N);  // re-orthogonalise
    vec3 B = cross(N, T) * inTangent.w;

    fragNormal   = N;
    fragTexCoord = inTexCoord;
    fragTBN      = mat3(T, B, N);

    gl_Position  = frame.viewProjection * worldPos;
}
