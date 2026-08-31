#version 450

layout(set = 0, binding = 0) uniform SceneUBO {
    mat4 u_viewProj;
    vec4 u_lightDir;
    vec4 u_lightColor;
    vec4 u_ambient;
} ubo;

layout(push_constant) uniform Push {
    mat4 u_model;
    vec4 u_albedo;
    vec4 u_params;
} pc;

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in vec4 aColor;
layout(location = 4) in vec4 aTangent;

layout(location = 0) out vec4 vColor;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec3 vWorldPos;
layout(location = 3) out vec2 vTexCoord;
layout(location = 4) out vec4 vTangent;

void main() {
    vec4 world = pc.u_model * vec4(aPos, 1.0);
    vWorldPos = world.xyz;
    vNormal = mat3(pc.u_model) * aNormal;
    vColor = aColor;
    vTexCoord = aTexCoord;
    vTangent = aTangent;
    gl_Position = ubo.u_viewProj * world;
}
