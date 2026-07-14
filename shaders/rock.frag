#version 450
#include "inc/common.glsl"
#define ATM_SCENE_SAMPLERS
#include "inc/atmosphere.glsl"
#include "inc/shadow.glsl"

layout(set = 0, binding = 2) uniform sampler2D lightmap;
layout(set = 1, binding = 2) uniform sampler2D rock_color;

layout(location = 0) in vec3 vNrm;
layout(location = 1) in vec3 vWorld;
layout(location = 2) in float vTint;
layout(location = 3) in float vLocalY;

layout(location = 0) out vec4 outc;

float h21(vec2 p){ p = fract(p * vec2(123.34, 456.21)); p += dot(p, p + 45.32); return fract(p.x * p.y); }

float vnoise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = h21(i);
    float b = h21(i + vec2(1.0, 0.0));
    float c = h21(i + vec2(0.0, 1.0));
    float d = h21(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float fbm(vec2 p)
{
    float v = 0.0;
    float a = 0.5;
    for (int k = 0; k < 4; k++) { v += a * vnoise(p); p *= 2.11; a *= 0.5; }
    return v;
}

void main()
{
    vec3 N = normalize(vNrm);
    vec3 L = normalize(u.sundir.xyz);
    float nd = max(dot(N, L), 0.0);
    float det = fbm(vWorld.xz * 1.7 + vWorld.y * 0.9);
    vec3 aN = abs(N);
    vec3 w = aN / (aN.x + aN.y + aN.z);
    vec3 tex = texture(rock_color, vWorld.yz * 0.14).rgb * w.x +
               texture(rock_color, vWorld.xz * 0.14).rgb * w.y +
               texture(rock_color, vWorld.xy * 0.14).rgb * w.z;
    vec3 base = tex * (0.6 + 0.5 * det) * (0.85 + 0.3 * vTint);
    float groundAO = smoothstep(-0.95, -0.1, vLocalY);
    base *= 0.45 + 0.55 * groundAO;
    vec2 lmuv = vWorld.xz / TSCALE + 0.5;
    float shadow = sun_visibility(vWorld, N);
    float ao = texture(lightmap, lmuv).g * (0.55 + 0.45 * groundAO);
    vec3 ambient = mix(u.ambient_horizon.rgb, u.ambient_zenith.rgb, N.y * 0.5 + 0.5) * ATM_PI;
    vec3 col = base * (u.sun_radiance.rgb * nd * shadow + ambient * ao);
    col = aerial(col, vWorld, u.campos.xyz, u.sundir.xyz);
    outc = vec4(col, 1.0);
}
