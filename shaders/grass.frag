#version 450
#include "inc/common.glsl"
#define ATM_SCENE_SAMPLERS
#include "inc/atmosphere.glsl"
#include "inc/shadow.glsl"

layout(set = 0, binding = 2) uniform sampler2D lightmap;

layout(location = 0) in vec2 vUV;
layout(location = 1) in float vFade;
layout(location = 2) in float vTint;
layout(location = 3) in float vDist;
layout(location = 4) in vec3 vWorld;

layout(location = 0) out vec4 outc;

float h11(float p){ p = fract(p * 0.1031); p *= p + 33.33; p *= p + p; return fract(p); }

float hashn(vec2 p){ return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

float vnoise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 s = f * f * (3.0 - 2.0 * f);
    return mix(mix(hashn(i), hashn(i + vec2(1.0, 0.0)), s.x),
               mix(hashn(i + vec2(0.0, 1.0)), hashn(i + vec2(1.0, 1.0)), s.x), s.y);
}

float macro_fbm(vec2 p)
{
    float v = 0.0;
    float amp = 0.5;
    for (int i = 0; i < 3; i++)
    {
        v += amp * vnoise(p);
        p = p * 2.03 + vec2(11.3, 7.9);
        amp *= 0.5;
    }
    return v;
}

void main()
{
    float sx = vUV.x * 6.0;
    float id = floor(sx) + floor(vTint * 97.0);
    float f = fract(sx) - 0.5;
    float sh = 0.45 + 0.55 * h11(id);
    float wdt = 0.42 * max(1.0 - vUV.y / sh, 0.0);
    float a = (vUV.y < sh && abs(f) < wdt) ? 1.0 : 0.0;
    a *= vFade;
    if (a <= 0.0) discard;
    vec3 dry = vec3(0.32, 0.27, 0.09);
    vec3 lush = vec3(0.05, 0.14, 0.03);
    vec3 hi = vec3(0.24, 0.42, 0.10);
    vec3 lo = mix(lush, dry, smoothstep(0.35, 0.85, vTint));
    vec3 albedo = mix(lo, hi, vUV.y);
    albedo *= 0.85 + 0.35 * vTint;
    float macro = macro_fbm(vWorld.xz * 0.0025 + 19.0);
    albedo *= mix(vec3(0.82, 0.72, 0.42), vec3(0.95, 1.0, 0.90), macro);
    vec2 lmuv = vWorld.xz / TSCALE + 0.5;
    float shadow = sun_visibility(vWorld, vec3(0.0, 1.0, 0.0));
    float ao = texture(lightmap, lmuv).g;
    vec3 sunCol = u.sun_radiance.rgb;
    vec3 ambCol = mix(u.ambient_horizon.rgb, u.ambient_zenith.rgb, 0.6) * ATM_PI;
    vec3 col = albedo * (sunCol * shadow * (0.25 + 0.55 * vUV.y) +
                         ambCol * (0.35 + 0.65 * ao) * (0.55 + 0.45 * shadow));
    col = aerial(col, vWorld, u.campos.xyz, u.sundir.xyz);
    outc = vec4(col, a);
}
