#ifndef SHADOW_GLSL
#define SHADOW_GLSL

layout(set = 0, binding = 6) uniform sampler2D horizon_a;
layout(set = 0, binding = 7) uniform sampler2D horizon_b;
layout(set = 0, binding = 8) uniform sampler2DArrayShadow shadow_cascades;

float horizon_visibility(vec3 world, vec3 L)
{
    vec2 uv = world.xz / TSCALE + 0.5;
    vec4 ha = texture(horizon_a, uv);
    vec4 hb = texture(horizon_b, uv);
    float h[8] = float[8](ha.x, ha.y, ha.z, ha.w, hb.x, hb.y, hb.z, hb.w);
    float t = atan(L.z, L.x) * (8.0 / 6.2831853);
    t -= 8.0 * floor(t * 0.125);
    int i0 = int(t) & 7;
    int i1 = (i0 + 1) & 7;
    float ang = mix(h[i0], h[i1], fract(t)) * 1.5707963;
    float e = asin(clamp(L.y, -1.0, 1.0));
    return smoothstep(ang - 0.02, ang + 0.06, e);
}

float csm_visibility(vec3 world, vec3 N, float dist)
{
    float nfar = TIER == 0u ? u.cascade_radii.y : u.cascade_radii.z;
    float fade = smoothstep(nfar * 0.80, nfar * 0.97, dist);
    if (fade >= 1.0) return 1.0;
    int ci;
    float texel;
    if (dist < u.cascade_radii.x * 0.9) { ci = 0; texel = u.cascade_texel.x; }
    else if (TIER == 0u || dist < u.cascade_radii.y * 0.9) { ci = 1; texel = u.cascade_texel.y; }
    else { ci = 2; texel = u.cascade_texel.z; }
    vec4 sp = u.cascade_vp[ci] * vec4(world + N * texel * 2.0, 1.0);
    vec2 suv = sp.xy * 0.5 + 0.5;
    float ref = sp.z - 0.0012;
    vec2 ts = 1.0 / vec2(textureSize(shadow_cascades, 0).xy);
    float sum = 0.0;
    for (int y = -1; y <= 1; y++)
        for (int x = -1; x <= 1; x++)
            sum += texture(shadow_cascades, vec4(suv + vec2(float(x), float(y)) * ts, float(ci), ref));
    return mix(sum * (1.0 / 9.0), 1.0, fade);
}

float sun_visibility(vec3 world, vec3 N)
{
    vec3 L = normalize(u.sundir.xyz);
    float vis = horizon_visibility(world, L);
    float dist = distance(world, u.campos.xyz);
    return min(vis, csm_visibility(world, N, dist));
}

#endif
