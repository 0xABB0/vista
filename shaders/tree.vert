#version 450
#include "inc/common.glsl"

layout(set = 0, binding = 1) uniform sampler2D heightmap;
layout(set = 0, binding = 2) uniform sampler2D lightmap;

layout(push_constant) uniform PC { vec4 mode; } pc;

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNrm;
layout(location = 2) in vec3 aCol;
layout(location = 3) in vec4 i0;
layout(location = 4) in vec4 i1;

layout(location = 0) out vec3 vNrm;
layout(location = 1) out vec3 vWorld;
layout(location = 2) out vec3 vCol;
layout(location = 3) out float vShadow;
layout(location = 4) out float vAO;
layout(location = 5) out float vLocalH;
layout(location = 6) out float vShadowAlpha;

void main()
{
    if (pc.mode.x > 0.5)
    {
        vec3 sunH = vec3(u.sundir.x, 0.0, u.sundir.z);
        float sunHLen = length(sunH);
        sunH = sunHLen > 0.0001 ? sunH / sunHLen : vec3(0.0, 0.0, 1.0);
        vec2 perp = vec2(-sunH.z, sunH.x);
        float scale = i0.w;
        float elong = 1.0 + 1.6 / max(u.sundir.y, 0.15);
        vec2 foot = aPos.xz * scale * 1.1;
        float a = foot.x * sunH.x + foot.y * sunH.z;
        float b = foot.x * perp.x + foot.y * perp.y;
        float aE = a > 0.0 ? a * elong : a * 0.35;
        vec2 off = sunH.xz * aE + perp * (b * 0.8);
        vec2 worldxz = i0.xz + off;
        vec2 uv = worldxz / TSCALE + 0.5;
        float gy = textureLod(heightmap, uv, 0.0).r * THEIGHT + 0.06;
        float r = clamp(length(aPos.xz) / 1.4, 0.0, 1.0);
        float hfrac = clamp(aPos.y / 6.6, 0.0, 1.0);
        float alpha = clamp(1.0 - r, 0.0, 1.0) * (1.0 - hfrac * 0.7);
        float camDist = distance(i0.xyz, u.campos.xyz);
        alpha *= clamp(1.0 - camDist / 900.0, 0.0, 1.0);
        vShadowAlpha = alpha * 0.5;
        vWorld = vec3(worldxz.x, gy, worldxz.y);
        vNrm = vec3(0.0, 1.0, 0.0);
        vCol = vec3(0.0);
        vShadow = 1.0;
        vAO = 1.0;
        vLocalH = 0.0;
        if (camDist < 2.2)
        {
            gl_Position = vec4(0.0);
            return;
        }
        gl_Position = u.viewproj[VIEW] * vec4(vWorld, 1.0);
        return;
    }
    float yaw = i1.x;
    float cy = cos(yaw);
    float sy = sin(yaw);
    float scale = i0.w;
    vec3 p = aPos * scale;
    vec3 rp = vec3(cy * p.x + sy * p.z, p.y, -sy * p.x + cy * p.z);
    vec3 rn = vec3(cy * aNrm.x + sy * aNrm.z, aNrm.y, -sy * aNrm.x + cy * aNrm.z);
    float t = U_TIME;
    float hfrac = clamp(aPos.y / 6.6, 0.0, 1.0);
    float sway = hfrac * hfrac * 0.16 * scale;
    float ph = i1.y * 6.2831853;
    rp.x += sin(t * 1.3 + ph) * sway;
    rp.z += cos(t * 1.1 + ph * 1.7) * sway;
    vec3 world = i0.xyz + rp;
    vec2 lmuv = i0.xz / TSCALE + 0.5;
    vec2 lm = textureLod(lightmap, lmuv, 0.0).rg;
    vShadow = lm.r;
    vAO = lm.g;
    vNrm = rn;
    vWorld = world;
    vLocalH = hfrac;
    vec3 hueA = vec3(1.08, 0.97, 0.85);
    vec3 hueB = vec3(0.90, 1.05, 0.97);
    vCol = aCol * mix(hueA, hueB, i1.y);
    vShadowAlpha = 0.0;
    float camDist = distance(i0.xyz, u.campos.xyz);
    if (camDist < 2.2)
    {
        gl_Position = vec4(0.0);
        return;
    }
    gl_Position = u.viewproj[VIEW] * vec4(world, 1.0);
}
