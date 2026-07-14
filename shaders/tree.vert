#version 450
#include "inc/common.glsl"

layout(set = 0, binding = 2) uniform sampler2D lightmap;

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNrm;
layout(location = 2) in vec3 aCol;
layout(location = 3) in vec4 i0;
layout(location = 4) in vec4 i1;

layout(location = 0) out vec3 vNrm;
layout(location = 1) out vec3 vWorld;
layout(location = 2) out vec3 vCol;
layout(location = 3) out float vAO;
layout(location = 4) out float vLocalH;

void main()
{
    float yaw = i1.x;
    float cy = cos(yaw);
    float sy = sin(yaw);
    float scale = i0.w;
    vec3 p = aPos * scale;
    vec3 rp = vec3(cy * p.x + sy * p.z, p.y, -sy * p.x + cy * p.z);
    vec3 rn = vec3(cy * aNrm.x + sy * aNrm.z, aNrm.y, -sy * aNrm.x + cy * aNrm.z);
    float t = U_TIME;
    float hfrac = clamp(aPos.y / 6.6, 0.0, 1.0);
    float sway = hfrac * hfrac * 0.16 * scale;
    float ph = i1.y * 6.2831853;
    rp.x += sin(t * 1.3 + ph) * sway;
    rp.z += cos(t * 1.1 + ph * 1.7) * sway;
    vec3 world = i0.xyz + rp;
    vec2 lmuv = i0.xz / TSCALE + 0.5;
    vAO = textureLod(lightmap, lmuv, 0.0).g;
    vNrm = rn;
    vWorld = world;
    vLocalH = hfrac;
    vec3 hueA = vec3(1.08, 0.97, 0.85);
    vec3 hueB = vec3(0.90, 1.05, 0.97);
    vCol = aCol * mix(hueA, hueB, i1.y);
    float camDist = distance(i0.xyz, u.campos.xyz);
    if (camDist < 2.2)
    {
        gl_Position = vec4(0.0);
        return;
    }
    gl_Position = u.viewproj[VIEW] * vec4(world, 1.0);
}
