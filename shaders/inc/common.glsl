#ifndef COMMON_GLSL
#define COMMON_GLSL

#ifdef MULTIVIEW
#extension GL_EXT_multiview : require
#define VIEW gl_ViewIndex
#else
#define VIEW 0
#endif

layout(constant_id = 0) const uint TIER = 1u;

layout(set = 0, binding = 0) uniform FrameUBO {
    mat4 viewproj[2];
    vec4 campos;
    vec4 sundir;
    vec4 sun_radiance;
    vec4 ambient_zenith;
    vec4 ambient_horizon;
    vec4 timeparams;
    vec4 post;
    mat4 cascade_vp[3];
    vec4 cascade_radii;
    vec4 cascade_texel;
} u;

#define U_TIME (u.timeparams.x)
#define U_TODFRAC (u.timeparams.y)
#define U_QUALITY (u.timeparams.z)
#define U_TESSQ (u.timeparams.w)
#define U_EXPOSURE (u.sun_radiance.w)

const float TSCALE = 4000.0;
const float THEIGHT = 380.0;
const float WLEVEL = 58.0;

#endif
