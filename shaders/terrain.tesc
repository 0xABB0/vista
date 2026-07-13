#version 450
#include "inc/common.glsl"

layout(vertices = 4) out;

layout(set = 0, binding = 1) uniform sampler2D heightmap;

layout(location = 0) in vec2 v_xz[];
layout(location = 0) out vec2 t_xz[];

vec3 wpos(vec2 xz)
{
    vec2 uv = xz / TSCALE + 0.5;
    float h = textureLod(heightmap, uv, 0.0).r * THEIGHT;
    return vec3(xz.x, h, xz.y);
}

float edge_level(vec2 ea, vec2 eb)
{
    vec3 a = wpos(ea);
    vec3 b = wpos(eb);
    float len = distance(a, b);
    float d = max(distance(0.5 * (a + b), u.campos.xyz), 4.0);
    return clamp(U_TESSQ * 24.0 * len / d, 1.0, 48.0);
}

void main()
{
    t_xz[gl_InvocationID] = v_xz[gl_InvocationID];
    if (gl_InvocationID == 0)
    {
        float e0 = edge_level(v_xz[0], v_xz[3]);
        float e1 = edge_level(v_xz[0], v_xz[1]);
        float e2 = edge_level(v_xz[1], v_xz[2]);
        float e3 = edge_level(v_xz[3], v_xz[2]);
        gl_TessLevelOuter[0] = e0;
        gl_TessLevelOuter[1] = e1;
        gl_TessLevelOuter[2] = e2;
        gl_TessLevelOuter[3] = e3;
        gl_TessLevelInner[0] = max(e1, e3);
        gl_TessLevelInner[1] = max(e0, e2);
    }
}
