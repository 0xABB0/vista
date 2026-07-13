#version 450
#include "inc/common.glsl"

layout(quads, fractional_even_spacing, ccw) in;

layout(set = 0, binding = 1) uniform sampler2D heightmap;

layout(location = 0) in vec2 t_xz[];
layout(location = 0) out vec3 f_world;
layout(location = 1) out vec2 f_uv;

void main()
{
    vec2 a = mix(t_xz[0], t_xz[1], gl_TessCoord.x);
    vec2 b = mix(t_xz[3], t_xz[2], gl_TessCoord.x);
    vec2 xz = mix(a, b, gl_TessCoord.y);
    vec2 uv = xz / TSCALE + 0.5;
    float h = textureLod(heightmap, uv, 0.0).r * THEIGHT;
    vec3 wp = vec3(xz.x, h, xz.y);
    f_world = wp;
    f_uv = uv;
    gl_Position = u.viewproj[VIEW] * vec4(wp, 1.0);
}
