#version 450
#include "inc/common.glsl"

layout(push_constant) uniform PC { vec4 chunk; } pc;

layout(location = 0) out vec2 v_xz;

void main()
{
    const vec2 corners[4] = vec2[4](vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0));
    vec2 c = corners[gl_VertexIndex & 3];
    vec2 sub = vec2(float(gl_InstanceIndex & 3), float(gl_InstanceIndex >> 2));
    v_xz = pc.chunk.xy + (sub + c) * (pc.chunk.z * 0.25);
}
