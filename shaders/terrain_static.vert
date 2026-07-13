#version 450
#include "inc/common.glsl"

layout(push_constant) uniform PC { vec4 chunk; } pc;

layout(set = 0, binding = 1) uniform sampler2D heightmap;

layout(location = 0) out vec3 f_world;
layout(location = 1) out vec2 f_uv;

const int GRID = 65;

void main()
{
    int gx = gl_VertexIndex % GRID;
    int gz = gl_VertexIndex / GRID;
    vec2 xz = pc.chunk.xy + vec2(float(gx), float(gz)) * (pc.chunk.z / float(GRID - 1));
    vec2 uv = xz / TSCALE + 0.5;
    float h = textureLod(heightmap, uv, 0.0).r * THEIGHT;
    vec3 wp = vec3(xz.x, h, xz.y);
    f_world = wp;
    f_uv = uv;
    gl_Position = u.viewproj[VIEW] * vec4(wp, 1.0);
}
