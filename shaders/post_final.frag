#version 450
#include "inc/common.glsl"

#ifdef MULTIVIEW
layout(set = 1, binding = 0) uniform sampler2DArray hdr_scene;
layout(set = 1, binding = 1) uniform sampler2DArray bloom_tex;
#else
layout(set = 1, binding = 0) uniform sampler2D hdr_scene;
layout(set = 1, binding = 1) uniform sampler2D bloom_tex;
#endif

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

vec3 agx_contrast(vec3 x)
{
    vec3 x2 = x * x;
    vec3 x4 = x2 * x2;
    return 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x - 0.00232;
}

vec3 agx(vec3 val)
{
    const mat3 agx_mat = mat3(
        0.842479062253094, 0.0423282422610123, 0.0423756549057051,
        0.0784335999999992, 0.878468636469772, 0.0784336,
        0.0792237451477643, 0.0791661274605434, 0.879142973793104);
    const float min_ev = -12.47393;
    const float max_ev = 4.026069;
    val = agx_mat * val;
    val = clamp(log2(max(val, vec3(1e-10))), min_ev, max_ev);
    val = (val - min_ev) / (max_ev - min_ev);
    return agx_contrast(val);
}

vec3 agx_eotf(vec3 val)
{
    const mat3 agx_mat_inv = mat3(
        1.19687900512017, -0.0528968517574562, -0.0529716355144438,
        -0.0980208811401368, 1.15190312990417, -0.0980434501171241,
        -0.0990297440797205, -0.0989611768448433, 1.15107367264116);
    val = agx_mat_inv * val;
    return pow(max(val, vec3(0.0)), vec3(2.2));
}

vec3 grade(vec3 val)
{
    float luma = dot(val, vec3(0.2126, 0.7152, 0.0722));
    vec3 warm = vec3(1.03, 1.0, 0.96);
    vec3 cool = vec3(0.97, 0.99, 1.05);
    val *= mix(cool, warm, smoothstep(0.15, 0.7, luma));
    val = luma + 1.12 * (val - luma);
    return val;
}

float grain_hash(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

void main()
{
#ifdef MULTIVIEW
    vec3 hdr = texture(hdr_scene, vec3(v_uv, float(VIEW))).rgb;
#else
    vec3 hdr = texture(hdr_scene, v_uv).rgb;
#endif
    if (TIER >= 1u) {
#ifdef MULTIVIEW
        vec3 bloom = texture(bloom_tex, vec3(v_uv, float(VIEW))).rgb;
#else
        vec3 bloom = texture(bloom_tex, v_uv).rgb;
#endif
        hdr += max(bloom, vec3(0.0)) * 0.18;
    }
    vec3 col = hdr * U_EXPOSURE;
    col = agx(col);
    col = grade(col);
    col = agx_eotf(col);
    float vig = 1.0 - u.post.x * smoothstep(0.55, 1.35, length(v_uv - 0.5) * 1.6);
    col *= vig;
    float g = grain_hash(v_uv * vec2(1920.0, 1080.0) + fract(U_TIME) * 100.0) - 0.5;
    col += g * u.post.y;
    o_color = vec4(max(col, vec3(0.0)), 1.0);
}
