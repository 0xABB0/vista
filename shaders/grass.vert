#version 450
#include "inc/common.glsl"

layout(location = 0) in vec4 i0;
layout(location = 1) in vec4 i1;

layout(location = 0) out vec2 vUV;
layout(location = 1) out float vFade;
layout(location = 2) out float vTint;
layout(location = 3) out float vDist;
layout(location = 4) out vec3 vWorld;

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
    for (int k = 0; k < 4; k++) { v += a * vnoise(p); p *= 2.03; a *= 0.5; }
    return v;
}

const vec2 C[6] = vec2[6](
    vec2(-0.5, 0.0), vec2(0.5, 0.0), vec2(0.5, 1.0),
    vec2(-0.5, 0.0), vec2(0.5, 1.0), vec2(-0.5, 1.0)
);

void main()
{
    int q = gl_VertexIndex / 6;
    vec2 c = C[gl_VertexIndex % 6];
    float ang = i1.x + float(q) * 1.5707963;
    vec2 dir = vec2(cos(ang), sin(ang));
    float hgt = i0.w;
    vec3 base = i0.xyz;
    float dist = distance(base, u.campos.xyz);
    float nearFade = smoothstep(2.5, 9.0, dist);
    vec3 pos = base + vec3(dir.x * c.x * hgt * 0.65, c.y * hgt, dir.y * c.x * hgt * 0.65) * nearFade;
    float t = U_TIME;
    float bend = c.y * c.y;
    float w = fbm(base.xz * 0.13 + vec2(t * 0.8, t * 0.6)) * 2.0 - 1.0;
    pos.x += (w * 0.35 + 0.08 * sin(t * 2.3 + i1.y)) * bend * hgt * nearFade;
    pos.z += (w * 0.27 + 0.08 * cos(t * 1.9 + i1.y)) * bend * hgt * nearFade;
    vFade = nearFade * (1.0 - smoothstep(i1.w * 0.32, i1.w * 1.0, dist));
    vUV = vec2(c.x + 0.5, c.y);
    vTint = i1.z;
    vDist = dist;
    vWorld = pos;
    gl_Position = u.viewproj[VIEW] * vec4(pos, 1.0);
}
