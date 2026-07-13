#version 450
#include "inc/common.glsl"

layout(location = 0) out vec3 f_world;

const int GRID = 129;

void main()
{
    int gx = gl_VertexIndex % GRID;
    int gz = gl_VertexIndex / GRID;
    vec2 xz = (vec2(float(gx), float(gz)) / float(GRID - 1) - 0.5) * TSCALE;
    float t = U_TIME;
    float w0 = sin(xz.x * 0.045 + xz.y * 0.021 + t * 1.15) * 0.09;
    float w1 = sin(xz.x * 0.017 - xz.y * 0.029 - t * 0.68) * 0.06;
    vec3 wp = vec3(xz.x, WLEVEL + w0 + w1, xz.y);
    f_world = wp;
    gl_Position = u.viewproj[VIEW] * vec4(wp, 1.0);
}
