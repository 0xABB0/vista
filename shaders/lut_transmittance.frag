#version 450
#include "inc/common.glsl"
#include "inc/atmosphere.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

void main()
{
    float r, mu;
    atm_t_params(v_uv, r, mu);
    float tmax = atm_ray_top(r, mu);
    const int N = 40;
    float dt = tmax / float(N);
    vec3 tau = vec3(0.0);
    for (int i = 0; i < N; i++)
    {
        float t = (float(i) + 0.5) * dt;
        float rh = sqrt(r * r + t * t + 2.0 * r * mu * t);
        vec3 sr, sm, ext;
        atm_medium(rh - ATM_RG, sr, sm, ext);
        tau += ext * dt;
    }
    o_color = vec4(exp(-tau), 1.0);
}
