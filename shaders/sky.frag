#version 450
#include "inc/common.glsl"
#define ATM_SCENE_SAMPLERS
#include "inc/atmosphere.glsl"

layout(location = 0) in vec3 v_ray;
layout(location = 0) out vec4 o_color;

uint hash3u(ivec3 p)
{
    uint h = 2166136261u;
    h = (h ^ uint(p.x)) * 16777619u;
    h = (h ^ uint(p.y)) * 16777619u;
    h = (h ^ uint(p.z)) * 16777619u;
    h ^= h >> 15u; h *= 0x85ebca6bu;
    h ^= h >> 13u; h *= 0xc2b2ae35u;
    h ^= h >> 16u;
    return h;
}

float hash31(vec3 p)
{
    return float(hash3u(ivec3(floor(p))) & 0x00FFFFFFu) * (1.0 / 16777216.0);
}

float vnoise3(vec3 p)
{
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n000 = hash31(i + vec3(0.0, 0.0, 0.0));
    float n100 = hash31(i + vec3(1.0, 0.0, 0.0));
    float n010 = hash31(i + vec3(0.0, 1.0, 0.0));
    float n110 = hash31(i + vec3(1.0, 1.0, 0.0));
    float n001 = hash31(i + vec3(0.0, 0.0, 1.0));
    float n101 = hash31(i + vec3(1.0, 0.0, 1.0));
    float n011 = hash31(i + vec3(0.0, 1.0, 1.0));
    float n111 = hash31(i + vec3(1.0, 1.0, 1.0));
    float nx00 = mix(n000, n100, f.x);
    float nx10 = mix(n010, n110, f.x);
    float nx01 = mix(n001, n101, f.x);
    float nx11 = mix(n011, n111, f.x);
    float nxy0 = mix(nx00, nx10, f.y);
    float nxy1 = mix(nx01, nx11, f.y);
    return mix(nxy0, nxy1, f.z);
}

float fbm3(vec3 p, int octaves)
{
    float v = 0.0;
    float amp = 0.5;
    float sum = 0.0;
    for (int i = 0; i < octaves; i++)
    {
        v += amp * vnoise3(p);
        sum += amp;
        p = vec3(p.y, p.z, p.x) * 2.03 + vec3(19.19, 7.77, 4.34) * float(i + 1);
        amp *= 0.5;
    }
    return v / sum;
}

void main()
{
    vec3 dir = normalize(v_ray);
    vec3 sd = normalize(u.sundir.xyz);
    float t = U_TIME;
    float cosang = clamp(dot(dir, sd), -1.0, 1.0);

    vec3 col = atm_sky(dir, sd);

    vec3 sunCol = u.sun_radiance.rgb;
    vec3 skyAmb = (u.ambient_zenith.rgb + u.ambient_horizon.rgb) * 0.5;

    float upfade = smoothstep(-0.03, 0.12, dir.y);

    vec3 wind = normalize(vec3(1.0, 0.0, 0.4));
    vec3 wperp = normalize(cross(vec3(0.0, 1.0, 0.0), wind));
    vec3 windw = vec3(dot(dir, wind), dot(dir, wperp), dir.y);
    vec3 cw = windw * 5.0 + vec3(t * 0.028, t * 0.006, 0.0);
    vec3 cirrusp = vec3(cw.x * 0.35, cw.y * 1.6, cw.z * 1.6);
    float cirrusn = fbm3(cirrusp, 5);
    cirrusn += (vnoise3(cw * 6.0) - 0.5) * 0.05;
    float cirrusAA = fwidth(cirrusn) * 1.5 + 0.02;
    float cirrus = smoothstep(0.46 - cirrusAA, 0.72 + cirrusAA, cirrusn) * upfade;
    float cirrusEdge = smoothstep(0.46 - cirrusAA, 0.58, cirrusn) - smoothstep(0.58, 0.72 + cirrusAA, cirrusn);
    float cirrusSun = pow(max(cosang, 0.0), 5.0);
    vec3 cirrusCol = skyAmb * 1.6 + sunCol * (0.06 + 0.16 * cirrusSun);
    cirrusCol += sunCol * cirrusEdge * cirrusSun * 0.35;
    col = mix(col, cirrusCol, cirrus * 0.55);

    vec3 cuv2 = dir * vec3(4.4, 6.2, 5.3) + vec3(t * 0.010, 0.0, t * 0.004);
    float dens = fbm3(cuv2, 5);
    dens += (vnoise3(cuv2 * 11.0) - 0.5) * 0.06;
    float cloudAA = fwidth(dens) * 1.5 + 0.015;
    float cover = smoothstep(0.44 - cloudAA, 0.66 + cloudAA, dens) * upfade;
    float cumulusEdge = smoothstep(0.44 - cloudAA, 0.54, dens) - smoothstep(0.54, 0.66 + cloudAA, dens);
    float cumulusSun = pow(max(cosang, 0.0), 3.0);
    float shade = mix(0.35, 1.05, smoothstep(0.66, 0.30, dens));
    vec3 cumulusCol = skyAmb * 1.8 * shade + sunCol * cumulusSun * 0.10;
    cumulusCol += sunCol * cumulusEdge * pow(max(cosang, 0.0), 8.0) * 0.55;
    col = mix(col, cumulusCol, clamp(cover * 1.05, 0.0, 1.0));

    float disc = smoothstep(0.999955, 0.999989, cosang);
    if (disc > 0.0 && dir.y > -0.02)
    {
        float limb = 0.6 + 0.4 * sqrt(max(0.0, 1.0 - (1.0 - cosang) / 0.000011));
        vec3 sun = atm_sun_transmittance(dir, u.campos.y) * ATM_SUN * 40.0 * limb;
        col += sun * disc * (1.0 - clamp(cover * 1.4 + cirrus * 0.8, 0.0, 1.0));
    }

    o_color = vec4(col, 1.0);
}
