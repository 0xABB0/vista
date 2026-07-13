#include "vista.h"

#define ATM_RG 6360.0f
#define ATM_RT 6460.0f
#define ATM_RAY_H 8.0f
#define ATM_MIE_H 1.2f
#define ATM_PI 3.14159265f

static const float RAY_SCAT[3] = { 0.005802f, 0.013558f, 0.033100f };
static const float MIE_SCAT = 0.003996f;
static const float MIE_EXT = 0.004440f;
static const float OZO_ABS[3] = { 0.000650f, 0.001881f, 0.000085f };
static const float SUN_RAD[3] = { 8.0f, 7.6f, 7.4f };

static void atm_medium(float h, float sr[3], float *sm, float ext[3])
{
    if (h < 0.0f) h = 0.0f;
    float dr = expf(-h / ATM_RAY_H);
    float dm = expf(-h / ATM_MIE_H);
    float dz = 1.0f - fabsf(h - 25.0f) / 15.0f;
    if (dz < 0.0f) dz = 0.0f;
    for (int i = 0; i < 3; i++) {
        sr[i] = RAY_SCAT[i] * dr;
        ext[i] = RAY_SCAT[i] * dr + MIE_EXT * dm + OZO_ABS[i] * dz;
    }
    *sm = MIE_SCAT * dm;
}

static float ray_top(float r, float mu)
{
    float disc = r * r * (mu * mu - 1.0f) + ATM_RT * ATM_RT;
    if (disc < 0.0f) disc = 0.0f;
    float d = -r * mu + sqrtf(disc);
    return d > 0.0f ? d : 0.0f;
}

static bool hits_ground(float r, float mu)
{
    float disc = r * r * (mu * mu - 1.0f) + ATM_RG * ATM_RG;
    if (disc < 0.0f) return false;
    return -r * mu - sqrtf(disc) >= 0.0f;
}

static void transmittance(float r, float mu, float out[3])
{
    float tmax = ray_top(r, mu);
    const int N = 32;
    float dt = tmax / (float)N;
    float tau[3] = { 0, 0, 0 };
    for (int i = 0; i < N; i++) {
        float t = ((float)i + 0.5f) * dt;
        float rh = sqrtf(r * r + t * t + 2.0f * r * mu * t);
        float sr[3], sm, ext[3];
        atm_medium(rh - ATM_RG, sr, &sm, ext);
        for (int k = 0; k < 3; k++) tau[k] += ext[k] * dt;
    }
    for (int k = 0; k < 3; k++) out[k] = expf(-tau[k]);
}

static void sky_radiance(float dirx, float diry, float dirz, v3 sun, float r, float out[3])
{
    float mu = diry;
    float tmax;
    if (hits_ground(r, mu)) {
        float disc = r * r * (mu * mu - 1.0f) + ATM_RG * ATM_RG;
        tmax = -r * mu - sqrtf(disc > 0.0f ? disc : 0.0f);
    } else {
        tmax = ray_top(r, mu);
    }
    if (tmax > 9000.0f) tmax = 9000.0f;
    const int N = 14;
    float dt = tmax / (float)N;
    float cosvs = dirx * sun.x + diry * sun.y + dirz * sun.z;
    float pr = 3.0f / (16.0f * ATM_PI) * (1.0f + cosvs * cosvs);
    const float g = 0.8f;
    float pm = 3.0f / (8.0f * ATM_PI) * (1.0f - g * g) / (2.0f + g * g) *
               (1.0f + cosvs * cosvs) / powf(1.0f + g * g - 2.0f * g * cosvs, 1.5f);
    float through[3] = { 1, 1, 1 };
    float lum[3] = { 0, 0, 0 };
    for (int i = 0; i < N; i++) {
        float t = ((float)i + 0.5f) * dt;
        float px = dirx * t, py = r + diry * t, pz = dirz * t;
        float rh = sqrtf(px * px + py * py + pz * pz);
        float h = rh - ATM_RG;
        float sr[3], sm, ext[3];
        atm_medium(h, sr, &sm, ext);
        float cossun = (px * sun.x + py * sun.y + pz * sun.z) / rh;
        if (cossun > 1.0f) cossun = 1.0f;
        if (cossun < -1.0f) cossun = -1.0f;
        float tsun[3];
        if (hits_ground(rh, cossun)) {
            tsun[0] = tsun[1] = tsun[2] = 0.0f;
        } else {
            transmittance(rh, cossun, tsun);
        }
        float msboost = 0.9f;
        for (int k = 0; k < 3; k++) {
            float s = tsun[k] * (sr[k] * pr + sm * pm) + tsun[k] * msboost * (sr[k] + sm) * 0.25f / ATM_PI;
            float step_trans = expf(-ext[k] * dt);
            float integ = ext[k] > 1e-6f ? (s - s * step_trans) / ext[k] : s * dt;
            lum[k] += through[k] * integ;
            through[k] *= step_trans;
        }
    }
    for (int k = 0; k < 3; k++) out[k] = lum[k] * SUN_RAD[k];
}

void atmos_update(v3 sundir, float cam_y, FrameUBO *out)
{
    float r = ATM_RG + (cam_y > 500.0f ? cam_y : 500.0f) * 0.001f;
    float mu = sundir.y;
    if (mu > 1.0f) mu = 1.0f;
    if (mu < -1.0f) mu = -1.0f;
    float tsun[3] = { 0, 0, 0 };
    if (!hits_ground(r, mu))
        transmittance(r, mu, tsun);
    for (int k = 0; k < 3; k++)
        out->sun_radiance[k] = SUN_RAD[k] * tsun[k];

    float zen[3];
    sky_radiance(0.0f, 1.0f, 0.0f, sundir, r, zen);
    float hor[3] = { 0, 0, 0 };
    float sunaz_x = sundir.x, sunaz_z = sundir.z;
    float azlen = sqrtf(sunaz_x * sunaz_x + sunaz_z * sunaz_z);
    if (azlen < 1e-5f) { sunaz_x = 1.0f; sunaz_z = 0.0f; azlen = 1.0f; }
    sunaz_x /= azlen; sunaz_z /= azlen;
    const float elev = 0.25f;
    const float ce = 0.9689f;
    float azangles[4] = { 0.7f, 1.9f, 3.14159265f, 4.9f };
    for (int a = 0; a < 4; a++) {
        float ca = cosf(azangles[a]), sa = sinf(azangles[a]);
        float dx = (ca * sunaz_x - sa * sunaz_z) * ce;
        float dz = (sa * sunaz_x + ca * sunaz_z) * ce;
        float s[3];
        sky_radiance(dx, elev, dz, sundir, r, s);
        for (int k = 0; k < 3; k++) hor[k] += s[k] * 0.25f;
    }
    const float amb_boost = 1.6f;
    for (int k = 0; k < 3; k++) {
        out->ambient_zenith[k] = zen[k] * amb_boost;
        out->ambient_horizon[k] = hor[k] * amb_boost;
    }
    out->ambient_zenith[3] = 0.0f;
    out->ambient_horizon[3] = 0.0f;

    float skylum = 0.2126f * hor[0] + 0.7152f * hor[1] + 0.0722f * hor[2];
    float sunlum = 0.2126f * out->sun_radiance[0] + 0.7152f * out->sun_radiance[1] + 0.0722f * out->sun_radiance[2];
    float key = skylum * 2.4f + sunlum * 0.09f;
    if (key < 0.004f) key = 0.004f;
    float exposure = 0.55f / key;
    if (exposure > 45.0f) exposure = 45.0f;
    if (exposure < 0.05f) exposure = 0.05f;
    out->sun_radiance[3] = exposure;
}
