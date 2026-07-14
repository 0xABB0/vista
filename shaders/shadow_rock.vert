#version 450
#include "inc/common.glsl"
#include "inc/noise3.glsl"

layout(push_constant) uniform PC { mat4 vp; vec4 pad; } pc;

layout(location = 0) in vec3 aPos;
layout(location = 2) in vec4 i0;
layout(location = 3) in vec4 i1;

void main()
{
    float dist = distance(i0.xyz, u.campos.xyz);
    float sc = i0.w * (1.0 - smoothstep(i1.w * 0.85, i1.w, dist));
    float d = vnoise3(aPos * 2.1 + vec3(i1.y * 17.31));
    vec3 p = aPos * (0.72 + 0.56 * d);
    float cy = cos(i1.x);
    float sy = sin(i1.x);
    vec3 rp = vec3(cy * p.x + sy * p.z, p.y, -sy * p.x + cy * p.z);
    gl_Position = pc.vp * vec4(i0.xyz + rp * sc, 1.0);
}
