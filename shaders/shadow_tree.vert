#version 450
#include "inc/common.glsl"

layout(push_constant) uniform PC { mat4 vp; vec4 pad; } pc;

layout(location = 0) in vec3 aPos;
layout(location = 3) in vec4 i0;
layout(location = 4) in vec4 i1;

void main()
{
    float yaw = i1.x;
    float cy = cos(yaw);
    float sy = sin(yaw);
    vec3 p = aPos * i0.w;
    vec3 rp = vec3(cy * p.x + sy * p.z, p.y, -sy * p.x + cy * p.z);
    float t = U_TIME;
    float hfrac = clamp(aPos.y / 6.6, 0.0, 1.0);
    float sway = hfrac * hfrac * 0.16 * i0.w;
    float ph = i1.y * 6.2831853;
    rp.x += sin(t * 1.3 + ph) * sway;
    rp.z += cos(t * 1.1 + ph * 1.7) * sway;
    gl_Position = pc.vp * vec4(i0.xyz + rp, 1.0);
}
