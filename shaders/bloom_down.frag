#version 450
#include "inc/common.glsl"

#ifdef MULTIVIEW
layout(set = 1, binding = 0) uniform sampler2DArray src_tex;
#else
layout(set = 1, binding = 0) uniform sampler2D src_tex;
#endif

layout(push_constant) uniform PC { vec4 params; } pc;

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

vec3 fetch(vec2 uv)
{
#ifdef MULTIVIEW
    return texture(src_tex, vec3(uv, float(VIEW))).rgb;
#else
    return texture(src_tex, uv).rgb;
#endif
}

float karis_weight(vec3 c)
{
    return 1.0 / (1.0 + dot(c, vec3(0.2126, 0.7152, 0.0722)));
}

vec3 prefilter(vec3 c)
{
    float l = dot(c, vec3(0.2126, 0.7152, 0.0722)) * U_EXPOSURE;
    float soft = clamp(l - 0.5, 0.0, 1.0);
    soft = soft * soft * 0.5;
    float w = max(soft, l - 1.0) / max(l, 1e-4);
    return c * w;
}

void main()
{
    vec2 ts = pc.params.xy;
    vec3 a = fetch(v_uv + ts * vec2(-2.0, 2.0));
    vec3 b = fetch(v_uv + ts * vec2(0.0, 2.0));
    vec3 c = fetch(v_uv + ts * vec2(2.0, 2.0));
    vec3 d = fetch(v_uv + ts * vec2(-2.0, 0.0));
    vec3 e = fetch(v_uv);
    vec3 f = fetch(v_uv + ts * vec2(2.0, 0.0));
    vec3 g = fetch(v_uv + ts * vec2(-2.0, -2.0));
    vec3 h = fetch(v_uv + ts * vec2(0.0, -2.0));
    vec3 i = fetch(v_uv + ts * vec2(2.0, -2.0));
    vec3 j = fetch(v_uv + ts * vec2(-1.0, 1.0));
    vec3 k = fetch(v_uv + ts * vec2(1.0, 1.0));
    vec3 l = fetch(v_uv + ts * vec2(-1.0, -1.0));
    vec3 m = fetch(v_uv + ts * vec2(1.0, -1.0));
    vec3 g0 = (a + b + d + e) * 0.25;
    vec3 g1 = (b + c + e + f) * 0.25;
    vec3 g2 = (d + e + g + h) * 0.25;
    vec3 g3 = (e + f + h + i) * 0.25;
    vec3 g4 = (j + k + l + m) * 0.25;
    vec3 col;
    if (pc.params.z > 0.5) {
        g0 = prefilter(g0);
        g1 = prefilter(g1);
        g2 = prefilter(g2);
        g3 = prefilter(g3);
        g4 = prefilter(g4);
        float w0 = karis_weight(g0);
        float w1 = karis_weight(g1);
        float w2 = karis_weight(g2);
        float w3 = karis_weight(g3);
        float w4 = karis_weight(g4);
        float wsum = 0.125 * (w0 + w1 + w2 + w3) + 0.5 * w4;
        col = (0.125 * (g0 * w0 + g1 * w1 + g2 * w2 + g3 * w3) + 0.5 * g4 * w4) / wsum;
    } else {
        col = 0.125 * (g0 + g1 + g2 + g3) + 0.5 * g4;
    }
    o_color = vec4(max(col, vec3(0.0)), 1.0);
}
