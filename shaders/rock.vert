#version 450
#include "inc/common.glsl"

layout(set = 0, binding = 1) uniform sampler2D heightmap;

layout(push_constant) uniform PC { vec4 mode; } pc;

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNrm;
layout(location = 2) in vec4 i0;
layout(location = 3) in vec4 i1;

layout(location = 0) out vec3 vNrm;
layout(location = 1) out vec3 vWorld;
layout(location = 2) out float vTint;
layout(location = 3) out float vLocalY;
layout(location = 4) out float vShadowAlpha;

float h31(vec3 p)
{
    p = fract(p * 0.1031);
    p += dot(p, p.zyx + 31.32);
    return fract((p.x + p.y) * p.z);
}

float vnoise3(vec3 p)
{
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n000 = h31(i);
    float n100 = h31(i + vec3(1, 0, 0));
    float n010 = h31(i + vec3(0, 1, 0));
    float n110 = h31(i + vec3(1, 1, 0));
    float n001 = h31(i + vec3(0, 0, 1));
    float n101 = h31(i + vec3(1, 0, 1));
    float n011 = h31(i + vec3(0, 1, 1));
    float n111 = h31(i + vec3(1, 1, 1));
    return mix(mix(mix(n000, n100, f.x), mix(n010, n110, f.x), f.y),
               mix(mix(n001, n101, f.x), mix(n011, n111, f.x), f.y), f.z);
}

void main()
{
    float dist = distance(i0.xyz, u.campos.xyz);
    float sc = i0.w * (1.0 - smoothstep(i1.w * 0.85, i1.w, dist));
    if (pc.mode.x > 0.5)
    {
        vec3 sunH = vec3(u.sundir.x, 0.0, u.sundir.z);
        float sunHLen = length(sunH);
        sunH = sunHLen > 0.0001 ? sunH / sunHLen : vec3(0.0, 0.0, 1.0);
        vec2 perp = vec2(-sunH.z, sunH.x);
        float elong = 1.0 + 1.6 / max(u.sundir.y, 0.15);
        vec2 foot = aPos.xz * sc;
        float a = foot.x * sunH.x + foot.y * sunH.z;
        float b = foot.x * perp.x + foot.y * perp.y;
        float aE = a > 0.0 ? a * elong : a * 0.35;
        vec2 off = sunH.xz * aE + perp * (b * 0.8);
        vec2 worldxz = i0.xz + off;
        vec2 uv = worldxz / TSCALE + 0.5;
        float gy = textureLod(heightmap, uv, 0.0).r * THEIGHT + 0.06;
        float r = length(aPos.xz);
        float alpha = clamp(1.0 - r * 0.85, 0.0, 1.0);
        alpha *= clamp(1.0 - dist / i1.w, 0.0, 1.0);
        vShadowAlpha = alpha * 0.55;
        vWorld = vec3(worldxz.x, gy, worldxz.y);
        vNrm = vec3(0.0, 1.0, 0.0);
        vTint = 0.0;
        vLocalY = 0.0;
        gl_Position = u.viewproj[VIEW] * vec4(vWorld, 1.0);
        return;
    }
    float d = vnoise3(aPos * 2.1 + vec3(i1.y * 17.31));
    vec3 p = aPos * (0.72 + 0.56 * d);
    float cy = cos(i1.x);
    float sy = sin(i1.x);
    vec3 rp = vec3(cy * p.x + sy * p.z, p.y, -sy * p.x + cy * p.z);
    vec3 rn = vec3(cy * aNrm.x + sy * aNrm.z, aNrm.y, -sy * aNrm.x + cy * aNrm.z);
    vec3 world = i0.xyz + rp * sc;
    vNrm = rn;
    vWorld = world;
    vTint = i1.z;
    vLocalY = p.y;
    vShadowAlpha = 0.0;
    gl_Position = u.viewproj[VIEW] * vec4(world, 1.0);
}
