#ifndef NOISE3_GLSL
#define NOISE3_GLSL

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

#endif
