#version 450
#include "inc/common.glsl"
#include "inc/atmosphere.glsl"

layout(set = 1, binding = 0) uniform sampler2D t_lut;
layout(set = 1, binding = 1) uniform sampler2D ms_lut;

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

vec3 sample_ms(float rh, float cos_sun)
{
    vec2 uv = vec2(cos_sun * 0.5 + 0.5, clamp((rh - ATM_RG) / (ATM_RT - ATM_RG), 0.0, 1.0));
    return texture(ms_lut, uv).rgb;
}

void main()
{
    vec3 sd = normalize(u.sundir.xyz);
    vec3 dir = atm_sky_dir(v_uv, sd);
    float cam_km = max(u.campos.y, 0.5) * 0.001;
    float r = ATM_RG + cam_km + 0.0005;
    vec3 ro = vec3(0.0, r, 0.0);
    float mu = dir.y;
    float tground = atm_ray_ground(r, mu);
    float tmax = tground > 0.0 ? tground : atm_ray_top(r, mu);
    tmax = min(tmax, 9000.0);
    const int N = 32;
    float dt = tmax / float(N);
    float cos_view_sun = clamp(dot(dir, sd), -1.0, 1.0);
    float pr = atm_ray_phase(cos_view_sun);
    float pm = atm_mie_phase(cos_view_sun);
    vec3 through = vec3(1.0);
    vec3 lum = vec3(0.0);
    for (int i = 0; i < N; i++)
    {
        float t = (float(i) + 0.5) * dt;
        vec3 pos = ro + dir * t;
        float rh = length(pos);
        vec3 up = pos / rh;
        float h = rh - ATM_RG;
        vec3 sr, sm, ext;
        atm_medium(h, sr, sm, ext);
        vec3 step_trans = exp(-ext * dt);
        float cos_sun = clamp(dot(up, sd), -1.0, 1.0);
        vec3 tsun = texture(t_lut, atm_t_uv(rh, cos_sun)).rgb;
        float ground_sun = atm_ray_ground(rh, cos_sun) > 0.0 ? 0.0 : 1.0;
        vec3 phase_scat = sr * pr + sm * pm;
        vec3 ms = sample_ms(rh, cos_sun);
        vec3 s = tsun * ground_sun * phase_scat + ms * (sr + sm);
        vec3 integ = (s - s * step_trans) / max(ext, vec3(1e-6));
        lum += through * integ;
        through *= step_trans;
    }
    o_color = vec4(lum * ATM_SUN, 1.0);
}
