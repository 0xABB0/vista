#include "vista.h"

float pg_clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

float pg_smoothstep(float a, float b, float x)
{
    float t = pg_clamp01((x - a) / (b - a));
    return t * t * (3.0f - 2.0f * t);
}

static uint32_t hash2(uint32_t x, uint32_t z, uint32_t seed)
{
    uint32_t h = seed + x * 0x9E3779B1u + z * 0x85EBCA77u;
    h ^= h >> 15; h *= 0x2C1B3C6Du;
    h ^= h >> 12; h *= 0x297A2D39u;
    h ^= h >> 15;
    return h;
}

static float lattice(int x, int z, uint32_t seed)
{
    return (float)(hash2((uint32_t)x, (uint32_t)z, seed) & 0xFFFFFFu) * (1.0f / 16777215.0f);
}

float pg_vnoise2(float x, float z, uint32_t seed)
{
    float fx = floorf(x), fz = floorf(z);
    int xi = (int)fx, zi = (int)fz;
    float tx = x - fx, tz = z - fz;
    float sx = tx * tx * (3.0f - 2.0f * tx);
    float sz = tz * tz * (3.0f - 2.0f * tz);
    float a = lattice(xi, zi, seed);
    float b = lattice(xi + 1, zi, seed);
    float c = lattice(xi, zi + 1, seed);
    float d = lattice(xi + 1, zi + 1, seed);
    float m0 = a + (b - a) * sx;
    float m1 = c + (d - c) * sx;
    return m0 + (m1 - m0) * sz;
}

float pg_fbm2(float x, float z, int oct, uint32_t seed)
{
    float v = 0.0f, amp = 0.5f, sum = 0.0f;
    for (int i = 0; i < oct; i++)
    {
        v += amp * pg_vnoise2(x, z, seed + (uint32_t)i * 101u);
        sum += amp;
        float xr = 0.8f * x - 0.6f * z;
        float zr = 0.6f * x + 0.8f * z;
        x = xr * 2.01f + 17.3f;
        z = zr * 2.01f + 9.2f;
        amp *= 0.5f;
    }
    return v / sum;
}

float pg_ridged2(float x, float z, int oct, uint32_t seed)
{
    float v = 0.0f, amp = 0.5f, sum = 0.0f;
    for (int i = 0; i < oct; i++)
    {
        float n = pg_vnoise2(x, z, seed + (uint32_t)i * 313u);
        n = 1.0f - fabsf(2.0f * n - 1.0f);
        n *= n;
        v += amp * n;
        sum += amp;
        float xr = 0.8f * x - 0.6f * z;
        float zr = 0.6f * x + 0.8f * z;
        x = xr * 2.03f + 5.1f;
        z = zr * 2.03f + 13.7f;
        amp *= 0.5f;
    }
    return v / sum;
}

static uint32_t hash2b(int32_t x, int32_t z, uint32_t seed)
{
    uint32_t h = seed + (uint32_t)x * 0x27d4eb2fu + (uint32_t)z * 0x9e3779b9u;
    h ^= h >> 15; h *= 0x85ebca6bu;
    h ^= h >> 13; h *= 0xc2b2ae35u;
    h ^= h >> 16;
    return h;
}

static float latticeb(int x, int z, uint32_t seed)
{
    return (float)(hash2b(x, z, seed) & 0xFFFFFFu) * (1.0f / 16777215.0f);
}

static float vnoise2b(float x, float z, uint32_t seed)
{
    float fx = floorf(x), fz = floorf(z);
    int xi = (int)fx, zi = (int)fz;
    float tx = x - fx, tz = z - fz;
    float sx = tx * tx * (3.0f - 2.0f * tx);
    float sz = tz * tz * (3.0f - 2.0f * tz);
    float a = latticeb(xi, zi, seed);
    float b = latticeb(xi + 1, zi, seed);
    float c = latticeb(xi, zi + 1, seed);
    float d = latticeb(xi + 1, zi + 1, seed);
    float m0 = a + (b - a) * sx;
    float m1 = c + (d - c) * sx;
    return m0 + (m1 - m0) * sz;
}

static float fbm2b(float x, float z, int oct, uint32_t seed)
{
    float v = 0.0f, amp = 0.5f, sum = 0.0f;
    for (int i = 0; i < oct; i++)
    {
        v += amp * vnoise2b(x, z, seed + (uint32_t)i * 257u);
        sum += amp;
        float xr = x * 2.02f + 3.1f;
        float zr = z * 2.02f + 7.7f;
        x = xr; z = zr;
        amp *= 0.5f;
    }
    return v / sum;
}

float pg_forest_density(float wx, float wz)
{
    float regional = fbm2b(wx * 0.0009f, wz * 0.0009f, 2, 0xF0EE5Du);
    float clumps = fbm2b(wx * 0.0035f + 41.0f, wz * 0.0035f + 17.0f, 3, 0x1357Bu);
    return regional * 0.6f + clumps * 0.4f;
}

float pg_forest_mask(float wx, float wz)
{
    return pg_smoothstep(0.40f, 0.62f, pg_forest_density(wx, wz));
}

static float macro_hash(float px, float pz)
{
    float v = sinf(px * 127.1f + pz * 311.7f) * 43758.5453f;
    return v - floorf(v);
}

static float macro_vnoise(float x, float z)
{
    float ix = floorf(x), iz = floorf(z);
    float fx = x - ix, fz = z - iz;
    float sx = fx * fx * (3.0f - 2.0f * fx);
    float sz = fz * fz * (3.0f - 2.0f * fz);
    float a = macro_hash(ix, iz);
    float b = macro_hash(ix + 1.0f, iz);
    float c = macro_hash(ix, iz + 1.0f);
    float d = macro_hash(ix + 1.0f, iz + 1.0f);
    float m0 = a + (b - a) * sx;
    float m1 = c + (d - c) * sx;
    return m0 + (m1 - m0) * sz;
}

float pg_terrain_macro(float x, float z)
{
    float px = x * 0.0025f + 19.0f, pz = z * 0.0025f + 19.0f;
    float v = 0.0f, amp = 0.5f;
    for (int i = 0; i < 3; i++)
    {
        v += amp * macro_vnoise(px, pz);
        float nx = px * 2.03f + 11.3f;
        float nz = pz * 2.03f + 7.9f;
        px = nx; pz = nz;
        amp *= 0.5f;
    }
    return v;
}
