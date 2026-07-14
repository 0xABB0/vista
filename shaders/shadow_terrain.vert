#version 450
#include "inc/common.glsl"

layout(push_constant) uniform PC { mat4 vp; vec4 chunk; } pc;

layout(set = 0, binding = 1) uniform sampler2D heightmap;

const int GRID = 65;

void main()
{
    int gx = gl_VertexIndex % GRID;
    int gz = gl_VertexIndex / GRID;
    vec2 xz = pc.chunk.xy + vec2(float(gx), float(gz)) * (pc.chunk.z / float(GRID - 1));
    vec2 uv = xz / TSCALE + 0.5;
    float h = textureLod(heightmap, uv, 0.0).r * THEIGHT;
    gl_Position = pc.vp * vec4(xz.x, h, xz.y, 1.0);
}
