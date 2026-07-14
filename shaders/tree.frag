#version 450
#include "inc/common.glsl"
#define ATM_SCENE_SAMPLERS
#include "inc/atmosphere.glsl"
#include "inc/shadow.glsl"

layout(location = 0) in vec3 vNrm;
layout(location = 1) in vec3 vWorld;
layout(location = 2) in vec3 vCol;
layout(location = 3) in float vAO;
layout(location = 4) in float vLocalH;

layout(location = 0) out vec4 outc;

float hashn(vec2 p){ return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

float vnoise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 s = f * f * (3.0 - 2.0 * f);
    float a = hashn(i);
    float b = hashn(i + vec2(1.0, 0.0));
    float c = hashn(i + vec2(0.0, 1.0));
    float d = hashn(i + vec2(1.0, 1.0));
    return mix(mix(a, b, s.x), mix(c, d, s.x), s.y);
}

void main()
{
    vec3 N = normalize(vNrm);
    vec3 V = normalize(u.campos.xyz - vWorld);
    vec3 L = normalize(u.sundir.xyz);
    float ndRaw = dot(N, L);
    float band = smoothstep(-0.35, 0.75, ndRaw);
    float nd = band * band * (3.0 - 2.0 * band);

    float mottle = vnoise(vWorld.xz * 0.9 + vWorld.y * 0.4);
    float speck = 0.60 + 0.60 * mottle;

    float rim = pow(1.0 - clamp(dot(N, V), 0.0, 1.0), 3.0) * 0.18;

    float shadow = sun_visibility(vWorld, N);
    vec3 sunCol = u.sun_radiance.rgb;
    vec3 skyAmb = u.ambient_zenith.rgb * ATM_PI;
    vec3 bounceAmb = u.ambient_horizon.rgb * ATM_PI * vec3(0.65, 0.62, 0.40);
    vec3 hemi = mix(bounceAmb, skyAmb, N.y * 0.5 + 0.5);
    float baseAO = 0.55 + 0.45 * smoothstep(0.0, 0.35, vLocalH);
    vec3 ambient = hemi * (0.35 + 0.65 * vAO) * baseAO;

    float crownGrad = 0.82 + 0.30 * vLocalH;
    vec3 tint = vCol * speck * crownGrad;
    vec3 lit = tint * (sunCol * nd * shadow * baseAO + ambient) + tint * rim * dot(skyAmb, vec3(0.33));
    vec3 col = aerial(lit, vWorld, u.campos.xyz, u.sundir.xyz);
    outc = vec4(col, 1.0);
}
