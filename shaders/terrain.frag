#version 450
#include "inc/common.glsl"
#define ATM_SCENE_SAMPLERS
#include "inc/atmosphere.glsl"

layout(set = 0, binding = 1) uniform sampler2D heightmap;
layout(set = 0, binding = 2) uniform sampler2D lightmap;
layout(set = 1, binding = 0) uniform sampler2D grass_color;
layout(set = 1, binding = 1) uniform sampler2D grass_normal;
layout(set = 1, binding = 2) uniform sampler2D rock_color;
layout(set = 1, binding = 3) uniform sampler2D rock_normal;
layout(set = 1, binding = 4) uniform sampler2D dirt_color;
layout(set = 1, binding = 5) uniform sampler2D dirt_normal;

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

vec3 sample_at(sampler2D t, vec2 uv, float k)
{
    vec3 c1 = texture(t, uv).rgb;
    vec3 c2 = texture(t, uv * 0.27 + vec2(5.3, 9.1)).rgb;
    return mix(c1, c2, k);
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

    vec2 lmSample = texture(lightmap, f_uv).rg;
    float shadowTerm = lmSample.r;
    float aoTerm = lmSample.g;

    bool lowq = TIER == 0u;
    float detail = fbm(f_world.xz * 0.05);
    float dfine = lowq ? vnoise(f_world.xz * 0.6) : fbm(f_world.xz * 0.6);
    float slope = 1.0 - n.y;
    float height = f_world.y / THEIGHT;

    float rockw = clamp(smoothstep(0.20, 0.48, slope + (detail - 0.5) * 0.18) +
                        smoothstep(0.62, 0.85, height + (detail - 0.5) * 0.1), 0.0, 1.0);
    float dirtw = (1.0 - rockw) * smoothstep(0.32, 0.10, height) * (0.25 + 0.75 * detail);
    float grassw = max(1.0 - rockw - dirtw, 0.0);
    float wsum = rockw + dirtw + grassw;
    rockw /= wsum; dirtw /= wsum; grassw /= wsum;

    float k = clamp(detail + (dfine - 0.5) * 0.4, 0.0, 1.0);
    vec2 uvg = f_world.xz * 0.25;
    vec2 uvr = f_world.xz * 0.12;
    vec2 uvd = f_world.xz * 0.20;

    vec3 albedo;
    vec3 N;
    float rockRegion = fbm(f_world.xz * 0.0012 + 71.0);
    vec3 rockTint = mix(vec3(0.78, 0.80, 0.86), vec3(1.05, 0.92, 0.80), rockRegion);
    float rk = clamp(rockRegion + (dfine - 0.5) * 0.5, 0.0, 1.0);

    if (lowq)
    {
        albedo = grassw * texture(grass_color, uvg).rgb +
                 rockw * texture(rock_color, uvr).rgb * rockTint +
                 dirtw * texture(dirt_color, uvd).rgb;
        albedo *= 0.85 + 0.3 * dfine;
        N = n;
    }
    else
    {
        albedo = grassw * sample_at(grass_color, uvg, k) +
                 rockw * sample_at(rock_color, uvr, rk) * rockTint +
                 dirtw * texture(dirt_color, uvd).rgb;
        albedo *= 0.85 + 0.3 * dfine;
        vec3 nm = grassw * (sample_at(grass_normal, uvg, k) * 2.0 - 1.0) +
                  rockw * (sample_at(rock_normal, uvr, rk) * 2.0 - 1.0) +
                  dirtw * (texture(dirt_normal, uvd).rgb * 2.0 - 1.0);
        nm = normalize(nm);
        vec3 t = normalize(vec3(1.0, 0.0, 0.0) - n * n.x);
        vec3 bt = cross(t, n);
        N = normalize(t * nm.x + bt * nm.y + n * nm.z);
    }

    float macro = fbm(f_world.xz * 0.0025 + 19.0);
    albedo *= mix(vec3(0.82, 0.72, 0.42), vec3(0.95, 1.0, 0.90), macro);

    float snowMask = smoothstep(0.52, 0.64, height) * (1.0 - smoothstep(0.30, 0.50, slope));
    vec3 snowCol = vec3(0.90, 0.93, 0.98);
    albedo = mix(albedo, snowCol, clamp(snowMask, 0.0, 1.0));

    float wet = 1.0 - smoothstep(WLEVEL, WLEVEL + 2.5, f_world.y);
    albedo *= mix(1.0, 0.72, wet);
    albedo = mix(albedo, albedo * vec3(0.70, 0.80, 0.85), wet * 0.6);

    vec3 L = normalize(u.sundir.xyz);
    float ndl = max(dot(N, L), 0.0);
    vec3 suncol = u.sun_radiance.rgb;
    vec3 ambient = mix(u.ambient_horizon.rgb, u.ambient_zenith.rgb, N.y * 0.5 + 0.5) * ATM_PI;
    vec3 col = albedo * (suncol * ndl * shadowTerm +
                         ambient * (0.30 + 0.70 * aoTerm) * (0.55 + 0.45 * shadowTerm));

    col = aerial(col, f_world, u.campos.xyz, u.sundir.xyz);
    o_color = vec4(col, 1.0);
}
