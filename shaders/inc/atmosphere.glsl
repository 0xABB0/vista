#ifndef ATMOSPHERE_GLSL
#define ATMOSPHERE_GLSL

const float ATM_RG = 6360.0;
const float ATM_RT = 6460.0;
const vec3 ATM_RAY_SCAT = vec3(0.005802, 0.013558, 0.033100);
const float ATM_RAY_H = 8.0;
const vec3 ATM_MIE_SCAT = vec3(0.003996);
const vec3 ATM_MIE_EXT = vec3(0.004440);
const float ATM_MIE_H = 1.2;
const vec3 ATM_OZO_ABS = vec3(0.000650, 0.001881, 0.000085);
const vec3 ATM_SUN = vec3(8.0, 7.6, 7.4);
const float ATM_PI = 3.14159265;
const float ATM_AERIAL_KM = 3.2;

void atm_medium(float h, out vec3 scat_r, out vec3 scat_m, out vec3 ext)
{
    float dr = exp(-max(h, 0.0) / ATM_RAY_H);
    float dm = exp(-max(h, 0.0) / ATM_MIE_H);
    float dz = max(0.0, 1.0 - abs(h - 25.0) / 15.0);
    scat_r = ATM_RAY_SCAT * dr;
    scat_m = ATM_MIE_SCAT * dm;
    ext = ATM_RAY_SCAT * dr + ATM_MIE_EXT * dm + ATM_OZO_ABS * dz;
}

float atm_ray_top(float r, float mu)
{
    float disc = r * r * (mu * mu - 1.0) + ATM_RT * ATM_RT;
    return max(0.0, -r * mu + sqrt(max(disc, 0.0)));
}

float atm_ray_ground(float r, float mu)
{
    float disc = r * r * (mu * mu - 1.0) + ATM_RG * ATM_RG;
    if (disc < 0.0) return -1.0;
    float d = -r * mu - sqrt(disc);
    return d >= 0.0 ? d : -1.0;
}

vec2 atm_t_uv(float r, float mu)
{
    float H = sqrt(ATM_RT * ATM_RT - ATM_RG * ATM_RG);
    float rho = sqrt(max(r * r - ATM_RG * ATM_RG, 0.0));
    float d = atm_ray_top(r, mu);
    float dmin = ATM_RT - r;
    float dmax = rho + H;
    return vec2((d - dmin) / (dmax - dmin), rho / H);
}

void atm_t_params(vec2 uv, out float r, out float mu)
{
    float H = sqrt(ATM_RT * ATM_RT - ATM_RG * ATM_RG);
    float rho = H * uv.y;
    r = sqrt(rho * rho + ATM_RG * ATM_RG);
    float dmin = ATM_RT - r;
    float dmax = rho + H;
    float d = dmin + uv.x * (dmax - dmin);
    mu = d == 0.0 ? 1.0 : (H * H - rho * rho - d * d) / (2.0 * r * d);
    mu = clamp(mu, -1.0, 1.0);
}

float atm_ray_phase(float c)
{
    return 3.0 / (16.0 * ATM_PI) * (1.0 + c * c);
}

float atm_mie_phase(float c)
{
    const float g = 0.8;
    float k = 3.0 / (8.0 * ATM_PI) * (1.0 - g * g) / (2.0 + g * g);
    return k * (1.0 + c * c) / pow(1.0 + g * g - 2.0 * g * c, 1.5);
}

vec2 atm_sky_uv(vec3 dir, vec3 sundir)
{
    float e = asin(clamp(dir.y, -1.0, 1.0));
    float s = sign(e) * sqrt(abs(e) / (ATM_PI * 0.5));
    float y = clamp(0.5 + 0.5 * s, 0.0, 1.0);
    vec2 sh = normalize(sundir.xz + vec2(1e-5, 0.0));
    vec2 dh = normalize(dir.xz + vec2(1e-5, 0.0));
    float ca = clamp(dot(sh, dh), -1.0, 1.0);
    float x = acos(ca) / ATM_PI;
    return vec2(x, y);
}

vec3 atm_sky_dir(vec2 uv, vec3 sundir)
{
    float s = (uv.y - 0.5) * 2.0;
    float e = sign(s) * s * s * (ATM_PI * 0.5);
    float a = uv.x * ATM_PI;
    vec2 sh = normalize(sundir.xz + vec2(1e-5, 0.0));
    float caz = cos(a), saz = sin(a);
    vec2 dh = vec2(caz * sh.x - saz * sh.y, saz * sh.x + caz * sh.y);
    float ce = cos(e);
    return normalize(vec3(dh.x * ce, sin(e), dh.y * ce));
}

#ifdef ATM_SCENE_SAMPLERS
layout(set = 0, binding = 3) uniform sampler2D atm_t_lut;
layout(set = 0, binding = 5) uniform sampler2D atm_sky_lut;

vec3 atm_sky(vec3 dir, vec3 sundir)
{
    return texture(atm_sky_lut, atm_sky_uv(dir, sundir)).rgb;
}

vec3 atm_sun_transmittance(vec3 dir, float cam_y)
{
    float r = ATM_RG + max(cam_y, 0.5) * 0.001 + 0.0005;
    return texture(atm_t_lut, atm_t_uv(r, clamp(dir.y, -1.0, 1.0))).rgb;
}

vec3 aerial(vec3 col, vec3 wpos, vec3 cam, vec3 sd)
{
    vec3 dv = wpos - cam;
    float dist_km = length(dv) * 0.001 * ATM_AERIAL_KM;
    vec3 dir = normalize(dv + vec3(0.0, 1e-4, 0.0));
    float h = max(cam.y, 0.0) * 0.001 + 0.25;
    vec3 sr, sm, ext;
    atm_medium(h, sr, sm, ext);
    vec3 tr = exp(-ext * dist_km);
    vec3 inscat = atm_sky(dir, sd) * (1.0 - tr);
    return col * tr + inscat;
}
#endif

#endif
