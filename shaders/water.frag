#version 450
#include "inc/common.glsl"
#define ATM_SCENE_SAMPLERS
#include "inc/atmosphere.glsl"
#include "inc/shadow.glsl"

layout(set = 0, binding = 1) uniform sampler2D heightmap;

layout(location = 0) in vec3 f_world;
layout(location = 0) out vec4 o_color;

uint hashu(int x, int z, uint seed)
{
    uint h = seed + uint(x) * 0x9E3779B1u + uint(z) * 0x85EBCA77u;
    h ^= h >> 15u; h *= 0x2C1B3C6Du;
    h ^= h >> 12u; h *= 0x297A2D39u;
    h ^= h >> 15u;
    return h;
}

float hashn(vec2 p)
{
    uint h = hashu(int(p.x), int(p.y), 1337u);
    return float(h & 0xFFFFFFu) * (1.0 / 16777215.0);
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

vec2 noise_grad(vec2 p, float eps)
{
    float n0 = vnoise(p);
    float nx = vnoise(p + vec2(eps, 0.0));
    float nz = vnoise(p + vec2(0.0, eps));
    return vec2(nx - n0, nz - n0) / eps;
}

void main()
{
    vec3 sd = normalize(u.sundir.xyz);
    float sunh = clamp(sd.y, -1.0, 1.0);
    float t = U_TIME;

    vec2 p1 = f_world.xz * 0.150 + vec2(0.9, 0.35) * t;
    vec2 p2 = f_world.xz * 0.450 + vec2(-0.55, 0.85) * t;
    vec2 p3 = f_world.xz * 1.300 + vec2(0.70, -0.60) * t;
    vec2 g1 = noise_grad(p1, 0.35);
    vec2 g2 = noise_grad(p2, 0.35);
    vec2 g3 = noise_grad(p3, 0.35);
    vec2 wave = g1 * 0.5 + g2 * 0.3 + g3 * 0.15;

    float dist = distance(f_world, u.campos.xyz);
    float rippleFade = clamp(1.0 - dist * 0.0016, 0.1, 1.0);
    vec2 slope = wave * rippleFade;
    float slopeLen = length(slope);
    float maxSlope = 0.16;
    if (slopeLen > maxSlope) slope *= maxSlope / slopeLen;
    vec3 N = normalize(vec3(-slope.x, 3.4, -slope.y));
    vec3 Ncalm = normalize(mix(vec3(0.0, 1.0, 0.0), N, 0.25));

    vec2 gslope = wave * clamp(1.0 - dist * 0.0005, 0.25, 1.0);
    float gslopeLen = length(gslope);
    if (gslopeLen > maxSlope) gslope *= maxSlope / gslopeLen;
    vec3 Ng = normalize(vec3(-gslope.x, 1.1, -gslope.y));

    vec2 uv = f_world.xz / TSCALE + 0.5;

    vec3 V = normalize(u.campos.xyz - f_world);
    float NdotV = clamp(dot(N, V), 0.0, 1.0);
    float fresnel = 0.02 + 0.90 * pow(1.0 - NdotV, 5.0);

    vec3 R = reflect(-V, N);
    vec3 Rc = reflect(-V, Ncalm);
    vec3 sky = atm_sky(vec3(Rc.x, max(Rc.y, 0.01), Rc.z), sd);
    float sunvis = smoothstep(-0.08, 0.02, sunh);
    float spec = pow(max(dot(R, sd), 0.0), 140.0);
    sky += u.sun_radiance.rgb * min(spec, 1.4) * 0.35 * sunvis;

    bool lowq = TIER == 0u;
    float mtnHit = 0.0;
    vec3 mtnCol = vec3(0.0);
    if (Rc.y > 0.003)
    {
        int steps = lowq ? 14 : 28;
        float rt = 8.0;
        float rstep = 10.0;
        for (int i = 0; i < steps; i++)
        {
            vec3 q = f_world + Rc * rt;
            if (q.y > THEIGHT * 1.05) break;
            float gh = textureLod(heightmap, q.xz / TSCALE + 0.5, 0.0).r * THEIGHT;
            if (gh >= q.y)
            {
                mtnHit = 1.0;
                float hn = clamp(q.y / THEIGHT, 0.0, 1.0);
                vec3 amb = mix(u.ambient_horizon.rgb, u.ambient_zenith.rgb, 0.5) * ATM_PI;
                vec3 rockc = vec3(0.22, 0.20, 0.18);
                vec3 grassc = vec3(0.10, 0.16, 0.08);
                vec3 snowc = vec3(0.85, 0.88, 0.95);
                vec3 mc = mix(grassc, rockc, smoothstep(0.34, 0.60, hn));
                mc = mix(mc, snowc, smoothstep(0.66, 0.76, hn));
                mc *= amb + u.sun_radiance.rgb * 0.15;
                mtnCol = aerial(mc, q, f_world, sd);
                break;
            }
            rt += rstep;
            rstep *= 1.22;
            if (rt > 3400.0) break;
        }
    }
    sky = mix(sky, mtnCol, mtnHit);

    float shadow = sun_visibility(f_world, vec3(0.0, 1.0, 0.0));
    sky *= 0.65 + 0.35 * shadow;

    vec3 deep = vec3(0.015, 0.07, 0.09) * mix(u.ambient_horizon.rgb, u.ambient_zenith.rgb, 0.5) * ATM_PI * 3.0 + sky * 0.20;
    vec3 col = mix(deep, sky, clamp(fresnel + 0.10, 0.0, 1.0));

    float terrainH = textureLod(heightmap, uv, 0.0).r * THEIGHT;
    float depth = WLEVEL - terrainH;
    float shallowT = clamp(depth / 1.5, 0.0, 1.0);
    vec3 amb2 = mix(u.ambient_horizon.rgb, u.ambient_zenith.rgb, 0.5) * ATM_PI;
    vec3 teal = vec3(0.10, 0.46, 0.42) * (amb2 + u.sun_radiance.rgb * 0.2);
    col = mix(teal, col, shallowT);
    float alpha = mix(0.15, 0.92, shallowT);

    float foamN = vnoise(f_world.xz * 0.35 + t * vec2(0.4, -0.25));
    float foamBand = smoothstep(0.5, -0.1, depth) * smoothstep(0.20, 0.55, foamN);
    vec3 foamCol = vec3(0.9) * (amb2 + u.sun_radiance.rgb * 0.5);
    col = mix(col, foamCol, foamBand * 0.9);
    alpha = max(alpha, foamBand * 0.85);

    vec3 H = normalize(V + sd);
    float gdot = clamp(dot(Ng, H), 0.0, 1.0);
    float glint = pow(gdot, 900.0) * 2.0 + pow(gdot, 64.0) * 0.08;
    float fresH = 0.02 + 0.98 * pow(1.0 - clamp(dot(H, V), 0.0, 1.0), 5.0);
    vec3 glintCol = u.sun_radiance.rgb * 40.0 * glint * fresH * sunvis * (0.65 + 0.35 * shadow);
    col += glintCol;
    float glintLum = dot(glintCol, vec3(0.2126, 0.7152, 0.0722));
    alpha = max(alpha, clamp(glintLum, 0.0, 0.95));

    col = aerial(col, f_world, u.campos.xyz, sd);
    o_color = vec4(col, alpha);
}
