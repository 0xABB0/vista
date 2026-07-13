#version 450
#include "inc/common.glsl"

layout(location = 0) out vec3 v_ray;

void main()
{
    vec2 ndc = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2)) * 2.0 - 1.0;
    mat4 vp = u.viewproj[VIEW];
    mat3 A = mat3(
        vec3(vp[0][0], vp[0][1], vp[0][3]),
        vec3(vp[1][0], vp[1][1], vp[1][3]),
        vec3(vp[2][0], vp[2][1], vp[2][3]));
    v_ray = inverse(A) * vec3(ndc, 1.0);
    gl_Position = vec4(ndc, 1.0, 1.0);
}
