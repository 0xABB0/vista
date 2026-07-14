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

void main()
{
    vec2 ts = pc.params.xy;
    vec3 col = fetch(v_uv + ts * vec2(-1.0, 1.0));
    col += 2.0 * fetch(v_uv + ts * vec2(0.0, 1.0));
    col += fetch(v_uv + ts * vec2(1.0, 1.0));
    col += 2.0 * fetch(v_uv + ts * vec2(-1.0, 0.0));
    col += 4.0 * fetch(v_uv);
    col += 2.0 * fetch(v_uv + ts * vec2(1.0, 0.0));
    col += fetch(v_uv + ts * vec2(-1.0, -1.0));
    col += 2.0 * fetch(v_uv + ts * vec2(0.0, -1.0));
    col += fetch(v_uv + ts * vec2(1.0, -1.0));
    o_color = vec4(max(col * (1.0 / 16.0), vec3(0.0)), 1.0);
}
