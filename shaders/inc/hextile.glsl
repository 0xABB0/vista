#ifndef HEXTILE_GLSL
#define HEXTILE_GLSL

struct HexTile {
    vec2 uv0;
    vec2 uv1;
    vec2 uv2;
    vec3 w;
};

vec2 ht_hash2(vec2 p)
{
    return fract(sin(vec2(dot(p, vec2(127.1, 311.7)),
                          dot(p, vec2(269.5, 183.3)))) * 43758.5453);
}

HexTile ht_grid(vec2 uv)
{
    const mat2 skew = mat2(1.0, 0.0, -0.57735027, 1.15470054);
    vec2 sk = skew * (uv * 3.4641016);
    vec2 base = floor(sk);
    vec3 t = vec3(fract(sk), 0.0);
    t.z = 1.0 - t.x - t.y;
    HexTile h;
    vec2 v0, v1, v2;
    if (t.z > 0.0) {
        h.w = vec3(t.z, t.y, t.x);
        v0 = base;
        v1 = base + vec2(0.0, 1.0);
        v2 = base + vec2(1.0, 0.0);
    } else {
        h.w = vec3(-t.z, 1.0 - t.y, 1.0 - t.x);
        v0 = base + vec2(1.0, 1.0);
        v1 = base + vec2(1.0, 0.0);
        v2 = base + vec2(0.0, 1.0);
    }
    vec3 e = h.w * h.w * h.w;
    h.w = e / (e.x + e.y + e.z);
    h.uv0 = fract(uv + ht_hash2(v0));
    h.uv1 = fract(uv + ht_hash2(v1));
    h.uv2 = fract(uv + ht_hash2(v2));
    return h;
}

vec4 ht_tex3(sampler2D t, HexTile h, vec2 dx, vec2 dy)
{
    return textureGrad(t, h.uv0, dx, dy) * h.w.x +
           textureGrad(t, h.uv1, dx, dy) * h.w.y +
           textureGrad(t, h.uv2, dx, dy) * h.w.z;
}

vec2 ht_uv1(vec2 uv)
{
    HexTile h = ht_grid(uv);
    float r = fract(sin(dot(uv, vec2(419.2, 371.9))) * 34568.313);
    if (r < h.w.x) return h.uv0;
    if (r < h.w.x + h.w.y) return h.uv1;
    return h.uv2;
}

#endif
