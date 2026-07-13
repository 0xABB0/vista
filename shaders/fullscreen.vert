#version 450
#include "inc/common.glsl"

layout(location = 0) out vec2 v_uv;

void main()
{
    vec2 ndc = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2)) * 2.0 - 1.0;
    v_uv = ndc * 0.5 + 0.5;
    gl_Position = vec4(ndc, 0.0, 1.0);
}
