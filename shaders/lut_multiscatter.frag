#version 450
#include "inc/common.glsl"
#include "inc/atmosphere.glsl"

layout(set = 1, binding = 0) uniform sampler2D t_lut;

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

void main()
{
    float sun_mu = v_uv.x * 2.0 - 1.0;
    float r = ATM_RG + v_uv.y * (ATM_RT - ATM_RG) + 0.0005;
    vec3 sundir = vec3(sqrt(max(1.0 - sun_mu * sun_mu, 0.0)), sun_mu, 0.0);
    vec3 ro = vec3(0.0, r, 0.0);
    vec3 lum_total = vec3(0.0);
    vec3 fms_total = vec3(0.0);
    const int NDIR = 64;
    const float GOLDEN = 2.39996323;
    for (int d = 0; d < NDIR; d++)
    {
        float cy = 1.0 - 2.0 * (float(d) + 0.5) / float(NDIR);
        float sy = sqrt(max(1.0 - cy * cy, 0.0));
        float phi = float(d) * GOLDEN;
        vec3 dir = vec3(cos(phi) * sy, cy, sin(phi) * sy);
        float mu = dir.y;
        float tground = atm_ray_ground(r, mu);
        float tmax = tground > 0.0 ? tground : atm_ray_top(r, mu);
        const int N = 20;
        float dt = tmax / float(N);
        vec3 through = vec3(1.0);
        vec3 lum = vec3(0.0);
        vec3 fms = vec3(0.0);
        for (int i = 0; i < N; i++)
        {
            float t = (float(i) + 0.5) * dt;
            vec3 pos = ro + dir * t;
            float rh = length(pos);
            vec3 up = pos / rh;
            float h = rh - ATM_RG;
            vec3 sr, sm, ext;
            atm_medium(h, sr, sm, ext);
            vec3 scat = sr + sm;
            vec3 step_trans = exp(-ext * dt);
            float cos_sun = clamp(dot(up, sundir), -1.0, 1.0);
            vec3 tsun = texture(t_lut, atm_t_uv(rh, cos_sun)).rgb;
            float ground_sun = atm_ray_ground(rh, cos_sun) > 0.0 ? 0.0 : 1.0;
            vec3 integ = (vec3(1.0) - step_trans) / max(ext, vec3(1e-6));
            lum += through * scat * tsun * ground_sun * (1.0 / (4.0 * ATM_PI)) * integ;
            fms += through * scat * integ;
            through *= step_trans;
        }
        if (tground > 0.0)
        {
            vec3 pos = ro + dir * tground;
            vec3 up = normalize(pos);
            float cos_sun = clamp(dot(up, sundir), -1.0, 1.0);
            vec3 tsun = texture(t_lut, atm_t_uv(ATM_RG, cos_sun)).rgb;
            lum += through * tsun * max(cos_sun, 0.0) * 0.3 / ATM_PI;
        }
        lum_total += lum / float(NDIR);
        fms_total += fms / float(NDIR);
    }
    vec3 psi = lum_total / max(vec3(1.0) - fms_total, vec3(1e-4));
    o_color = vec4(psi, 1.0);
}
