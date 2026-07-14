#version 450
#include "inc/common.glsl"
#define ATM_SCENE_SAMPLERS
#include "inc/atmosphere.glsl"
#include "inc/shadow.glsl"
#include "inc/hextile.glsl"
#include "inc/noise3.glsl"

layout(set = 0, binding = 1) uniform sampler2D heightmap;
layout(set = 0, binding = 2) uniform sampler2D lightmap;
layout(set = 1, binding = 0) uniform sampler2D grass_color;
layout(set = 1, binding = 1) uniform sampler2D grass_normal;
layout(set = 1, binding = 2) uniform sampler2D rock_color;
layout(set = 1, binding = 3) uniform sampler2D rock_normal;
layout(set = 1, binding = 4) uniform sampler2D dirt_color;
layout(set = 1, binding = 5) uniform sampler2D dirt_normal;
layout(set = 1, binding = 6) uniform sampler2D snow_color;
layout(set = 1, binding = 7) uniform sampler2D snow_normal;

layout(location = 0) in vec3 f_world;
layout(location = 1) in vec2 f_uv;
layout(location = 0) out vec4 o_color;

const float TEXEL = 1.0 / 2048.0;

float hashn(vec2 p)
{
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

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

float fbm(vec2 p)
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

float luma(vec3 c)
{
    return dot(c, vec3(0.299, 0.587, 0.114));
}

const float NSTR = 0.3;

void grad_clamp(inout vec2 dx, inout vec2 dy)
{
    float a = dot(dx, dx);
    float b = dot(dy, dy);
    float mx = max(a, b);
    float mn = max(min(a, b), 1.5e-8);
    if (mx > mn * 64.0)
    {
        float s = sqrt(mn * 64.0 / mx);
        if (a > b) dx *= s;
        else dy *= s;
    }
}

void mat_sample(sampler2D ct, sampler2D nt, vec2 uv, vec2 dx, vec2 dy,
                out vec4 col, out vec3 tn)
{
    grad_clamp(dx, dy);
    if (TIER == 0u)
    {
        col = textureGrad(ct, ht_uv1(uv), dx, dy);
        tn = vec3(0.0, 0.0, 1.0);
        return;
    }
    HexTile h = ht_grid(uv);
    col = ht_tex3(ct, h, dx, dy);
    vec3 n0 = textureGrad(nt, h.uv0, dx, dy).rgb * 2.0 - 1.0;
    vec3 n1 = textureGrad(nt, h.uv1, dx, dy).rgb * 2.0 - 1.0;
    vec3 n2 = textureGrad(nt, h.uv2, dx, dy).rgb * 2.0 - 1.0;
    tn = normalize(n0 * h.w.x + n1 * h.w.y + n2 * h.w.z);
    tn.xy *= NSTR;
}

void main()
{
    float hl = textureLod(heightmap, f_uv - vec2(TEXEL, 0.0), 0.0).r;
    float hr = textureLod(heightmap, f_uv + vec2(TEXEL, 0.0), 0.0).r;
    float hd = textureLod(heightmap, f_uv - vec2(0.0, TEXEL), 0.0).r;
    float hu = textureLod(heightmap, f_uv + vec2(0.0, TEXEL), 0.0).r;
    float gscale = THEIGHT / (2.0 * TEXEL * TSCALE);
    float dhdx = (hr - hl) * gscale;
    float dhdz = (hu - hd) * gscale;
    vec3 n = normalize(vec3(-dhdx, 1.0, -dhdz));

    float shadowTerm = sun_visibility(f_world, n);
    float aoTerm = texture(lightmap, f_uv).g;

    float hl2 = textureLod(heightmap, f_uv - vec2(0.5 * TEXEL, 0.0), 0.0).r;
    float hr2 = textureLod(heightmap, f_uv + vec2(0.5 * TEXEL, 0.0), 0.0).r;
    float hd2 = textureLod(heightmap, f_uv - vec2(0.0, 0.5 * TEXEL), 0.0).r;
    float hu2 = textureLod(heightmap, f_uv + vec2(0.0, 0.5 * TEXEL), 0.0).r;
    float gscale2 = THEIGHT / (TEXEL * TSCALE);
    vec3 gn = normalize(vec3(-(hr2 - hl2) * gscale2, 1.0, -(hu2 - hd2) * gscale2));

    vec3 dpx = dFdx(f_world);
    vec3 dpy = dFdy(f_world);

    float detail = fbm(f_world.xz * 0.05);
    float slope = 1.0 - min(n.y, gn.y);
    float height = f_world.y / THEIGHT;

    float rockw = clamp(smoothstep(0.20, 0.48, slope + (detail - 0.5) * 0.18) +
                        smoothstep(0.62, 0.85, height + (detail - 0.5) * 0.1), 0.0, 1.0);
    float dirtw = (1.0 - rockw) * smoothstep(0.32, 0.10, height) * (0.25 + 0.75 * detail);
    float grassw = max(1.0 - rockw - dirtw, 0.0);

    float sdith = (vnoise3(f_world * 1.7) - 0.5) * 0.13;
    float spatch = (fbm(f_world.xz * 0.37 + 31.0) - 0.5) * 0.12;
    float snoww = smoothstep(0.50, 0.66, height + (detail - 0.5) * 0.16 + sdith + spatch) *
                  (1.0 - smoothstep(0.30, 0.60, slope + (detail - 0.5) * 0.10));
    grassw *= 1.0 - snoww;
    dirtw *= 1.0 - snoww;
    rockw *= 1.0 - snoww;
    float wsum = grassw + rockw + dirtw + snoww;
    vec4 wv = vec4(grassw, rockw, dirtw, snoww) / wsum;

    const float sg = 0.25;
    const float sr = 0.12;
    const float sd = 0.20;
    const float ss = 0.18;

    vec4 cg = vec4(0.0);
    vec4 cr = vec4(0.0);
    vec4 cd = vec4(0.0);
    vec4 cs = vec4(0.0);
    vec3 tng = vec3(0.0, 0.0, 1.0);
    vec3 tnr = vec3(0.0, 0.0, 1.0);
    vec3 tnd = vec3(0.0, 0.0, 1.0);
    vec3 tns = vec3(0.0, 0.0, 1.0);
    vec3 ntri = n;
    float tri = smoothstep(0.42, 0.60, slope);

    if (wv.x > 0.003)
        mat_sample(grass_color, grass_normal, f_world.xz * sg, dpx.xz * sg, dpy.xz * sg, cg, tng);
    if (wv.z > 0.003)
        mat_sample(dirt_color, dirt_normal, f_world.xz * sd, dpx.xz * sd, dpy.xz * sd, cd, tnd);
    float camdist = distance(u.campos.xyz, f_world);
    if (wv.w > 0.003)
    {
        mat_sample(snow_color, snow_normal, f_world.xz * ss, dpx.xz * ss, dpy.xz * ss, cs, tns);
        float snowfade = 1.0 - smoothstep(25.0, 80.0, camdist);
        if (snowfade > 0.0)
        {
            float sds = ss * 6.3;
            vec2 suv = fract(f_world.xz * sds);
            vec2 sdx = dpx.xz * sds;
            vec2 sdy = dpy.xz * sds;
            grad_clamp(sdx, sdy);
            float sdet = luma(textureGrad(snow_color, suv, sdx, sdy).rgb);
            float savg = luma(textureLod(snow_color, suv, 6.0).rgb);
            cs.rgb *= mix(1.0, (0.55 + 0.55 * sdet) / (0.55 + 0.55 * savg), snowfade);
            if (TIER != 0u)
            {
                vec3 sdn = textureGrad(snow_normal, fract(f_world.xz * sds + 0.37),
                                       sdx, sdy).rgb * 2.0 - 1.0;
                tns = normalize(tns + vec3(sdn.xy * (NSTR * 0.8 * snowfade), 0.0));
            }
        }
    }
    if (wv.y > 0.003)
    {
        vec4 cflat = vec4(0.0);
        if (tri < 1.0)
            mat_sample(rock_color, rock_normal, f_world.xz * sr, dpx.xz * sr, dpy.xz * sr, cflat, tnr);
        vec4 ctri = vec4(0.0);
        if (tri > 0.0)
        {
            vec3 bl = abs(gn.y < n.y ? gn : n);
            bl = bl * bl * bl * bl;
            bl /= bl.x + bl.y + bl.z;
            vec2 ux = fract(f_world.zy * sr);
            vec2 uy = fract(f_world.xz * sr);
            vec2 uz = fract(f_world.xy * sr);
            vec2 xdx = dpx.zy * sr, xdy = dpy.zy * sr;
            vec2 ydx = dpx.xz * sr, ydy = dpy.xz * sr;
            vec2 zdx = dpx.xy * sr, zdy = dpy.xy * sr;
            grad_clamp(xdx, xdy);
            grad_clamp(ydx, ydy);
            grad_clamp(zdx, zdy);
            ctri = textureGrad(rock_color, ux, xdx, xdy) * bl.x +
                   textureGrad(rock_color, uy, ydx, ydy) * bl.y +
                   textureGrad(rock_color, uz, zdx, zdy) * bl.z;
            if (TIER != 0u)
            {
                vec3 tx = textureGrad(rock_normal, ux, xdx, xdy).rgb * 2.0 - 1.0;
                vec3 ty = textureGrad(rock_normal, uy, ydx, ydy).rgb * 2.0 - 1.0;
                vec3 tz = textureGrad(rock_normal, uz, zdx, zdy).rgb * 2.0 - 1.0;
                tx.xy *= NSTR;
                ty.xy *= NSTR;
                tz.xy *= NSTR;
                tx.x *= n.x >= 0.0 ? 1.0 : -1.0;
                tz.x *= n.z >= 0.0 ? 1.0 : -1.0;
                ntri = normalize(vec3(0.0, tx.yx) * bl.x +
                                 vec3(ty.x, 0.0, ty.y) * bl.y +
                                 vec3(tz.xy, 0.0) * bl.z + n);
            }
        }
        cr = mix(cflat, ctri, tri);
    }

    vec4 hv = vec4(luma(cg.rgb), luma(cr.rgb), luma(cd.rgb), luma(cs.rgb));
    vec4 sv = wv * (0.4 + 0.6 * hv);
    float ma = max(max(sv.x, sv.y), max(sv.z, sv.w));
    vec4 bv = max(sv - ma + 0.25, 0.0) * step(0.003, wv);
    bv /= bv.x + bv.y + bv.z + bv.w;

    vec3 albedo = cg.rgb * bv.x + cr.rgb * bv.y + cd.rgb * bv.z + cs.rgb * bv.w;
    float rough = cg.a * bv.x + cr.a * bv.y + cd.a * bv.z + cs.a * bv.w;

    vec3 N = n;
    if (TIER != 0u)
    {
        vec3 tn = tng * bv.x + tnr * (bv.y * (1.0 - tri)) + tnd * bv.z + tns * bv.w;
        tn.z = max(tn.z, 1e-4);
        vec3 t = normalize(vec3(1.0, 0.0, 0.0) - n * n.x);
        vec3 bt = cross(t, n);
        vec3 Nsp = normalize(t * tn.x + bt * tn.y + n * tn.z);
        N = normalize(mix(Nsp, ntri, bv.y * tri));
    }

    float wet = 1.0 - smoothstep(WLEVEL, WLEVEL + 2.5, f_world.y);
    albedo *= mix(1.0, 0.72, wet);
    albedo = mix(albedo, albedo * vec3(0.70, 0.80, 0.85), wet * 0.6);
    rough = clamp(mix(rough, 0.12, wet * 0.85), 0.08, 1.0);

    vec3 L = normalize(u.sundir.xyz);
    vec3 V = normalize(u.campos.xyz - f_world);
    float ndl = max(dot(N, L), 0.0);
    float ndv = max(dot(N, V), 1e-3);
    vec3 H = normalize(L + V);
    float ndh = max(dot(N, H), 0.0);
    float vdh = max(dot(V, H), 0.0);
    vec3 suncol = u.sun_radiance.rgb;
    vec3 ambient = mix(u.ambient_horizon.rgb, u.ambient_zenith.rgb, N.y * 0.5 + 0.5) * ATM_PI;

    float a = rough * rough;
    float a2 = a * a;
    vec3 dNx = dFdx(N);
    vec3 dNy = dFdy(N);
    a2 = min(a2 + 0.5 * (dot(dNx, dNx) + dot(dNy, dNy)), 1.0);
    float dd = ndh * ndh * (a2 - 1.0) + 1.0;
    float D = a2 / (ATM_PI * dd * dd);
    float vis = 0.5 / max(mix(2.0 * ndl * ndv, ndl + ndv, a), 1e-3);
    float f0 = 0.02 + 0.025 * bv.y + 0.02 * bv.w + 0.015 * wet;
    float F = f0 + (1.0 - f0) * pow(1.0 - vdh, 5.0);
    float Fa = f0 + (1.0 - f0) * pow(1.0 - ndv, 5.0);

    float aowrap = 0.30 + 0.70 * aoTerm;
    vec3 col = albedo * (suncol * ndl * shadowTerm +
                         ambient * aowrap * (0.55 + 0.45 * shadowTerm));
    col += suncol * (D * vis * F * ndl * shadowTerm);
    col += ambient * (Fa * (1.0 - rough) * (1.0 - rough) * aowrap);

    if (bv.w > 0.01)
    {
        vec3 cell = floor(f_world * 5.0);
        vec3 sgn = normalize(N * 1.2 +
                             vec3(h31(cell), h31(cell + 17.3), h31(cell + 41.7)) * 2.0 - 1.0);
        float glint = pow(max(dot(sgn, H), 0.0), 220.0);
        float gate = step(0.82, h31(cell + 5.1));
        float fade = 1.0 - smoothstep(60.0, 150.0, camdist);
        col += suncol * (bv.w * gate * glint * fade * ndl * shadowTerm * 3.0);
    }

    col = aerial(col, f_world, u.campos.xyz, u.sundir.xyz);
    float dbg_ckx = mod(floor(f_world.x / 1.953125), 2.0);
    float dbg_ckz = mod(floor(f_world.z / 1.953125), 2.0);
    o_color = vec4(dbg_ckx, 0.0, dbg_ckz, 1.0);
}
