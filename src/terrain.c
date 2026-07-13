#include "vista.h"
#include <stdlib.h>
#include <string.h>

#define TERRAIN_PC_STAGES (VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT | VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
#define STATIC_GRID 64
#define STATIC_IDX (STATIC_GRID * STATIC_GRID * 6)
#define LIGHTMAP_N 1024
#define HEIGHTMAP_WARP_AMP 0.42f
#define HEIGHTMAP_CORE_FRAC 0.10f
#define HEIGHTMAP_MASK_START 0.88f
#define HEIGHTMAP_MASK_END 1.30f
#define HEIGHTMAP_HIST_BUCKETS 8192

typedef struct { float x0, z0, size, pad; } TerrainPC;
typedef struct { float a, b, c, d; } Plane;

static float terrain_height_blur_at(float x, float z);

static uint16_t g_hm[TERRAIN_N * TERRAIN_N];
static uint16_t g_hm_blur[TERRAIN_N * TERRAIN_N];
static float g_cmin[TERRAIN_CHUNKS * TERRAIN_CHUNKS];
static float g_cmax[TERRAIN_CHUNKS * TERRAIN_CHUNKS];
static VImg g_hmtex;
static uint8_t g_lightmap[LIGHTMAP_N * LIGHTMAP_N * 4];
static VImg g_lmtex;
static VkPipeline g_pipe;
static VkPipelineLayout g_layout;
static VBuf g_ib;
static bool g_tess;
static VkCore *g_core;

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

static float vnoise2(float x, float z, uint32_t seed)
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

static float fbm2(float x, float z, int oct, uint32_t seed)
{
    float v = 0.0f, amp = 0.5f, sum = 0.0f;
    for (int i = 0; i < oct; i++)
    {
        v += amp * vnoise2(x, z, seed + (uint32_t)i * 101u);
        sum += amp;
        float xr = 0.8f * x - 0.6f * z;
        float zr = 0.6f * x + 0.8f * z;
        x = xr * 2.01f + 17.3f;
        z = zr * 2.01f + 9.2f;
        amp *= 0.5f;
    }
    return v / sum;
}

static float ridged2(float x, float z, int oct, uint32_t seed)
{
    float v = 0.0f, amp = 0.5f, sum = 0.0f;
    for (int i = 0; i < oct; i++)
    {
        float n = vnoise2(x, z, seed + (uint32_t)i * 313u);
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

static float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static float smoothstepf(float a, float b, float x)
{
    float t = clamp01((x - a) / (b - a));
    return t * t * (3.0f - 2.0f * t);
}

static uint32_t canopy_hash(int32_t x, int32_t z, uint32_t seed)
{
    uint32_t h = seed + (uint32_t)x * 0x27d4eb2fu + (uint32_t)z * 0x9e3779b9u;
    h ^= h >> 15; h *= 0x85ebca6bu;
    h ^= h >> 13; h *= 0xc2b2ae35u;
    h ^= h >> 16;
    return h;
}

static float canopy_lattice(int x, int z, uint32_t seed)
{
    return (float)(canopy_hash(x, z, seed) & 0xFFFFFFu) * (1.0f / 16777215.0f);
}

static float canopy_vnoise(float x, float z, uint32_t seed)
{
    float fx = floorf(x), fz = floorf(z);
    int xi = (int)fx, zi = (int)fz;
    float tx = x - fx, tz = z - fz;
    float sx = tx * tx * (3.0f - 2.0f * tx);
    float sz = tz * tz * (3.0f - 2.0f * tz);
    float a = canopy_lattice(xi, zi, seed);
    float b = canopy_lattice(xi + 1, zi, seed);
    float c = canopy_lattice(xi, zi + 1, seed);
    float d = canopy_lattice(xi + 1, zi + 1, seed);
    float m0 = a + (b - a) * sx;
    float m1 = c + (d - c) * sx;
    return m0 + (m1 - m0) * sz;
}

static float canopy_fbm(float x, float z, int oct, uint32_t seed)
{
    float v = 0.0f, amp = 0.5f, sum = 0.0f;
    for (int i = 0; i < oct; i++)
    {
        v += amp * canopy_vnoise(x, z, seed + (uint32_t)i * 257u);
        sum += amp;
        float xr = x * 2.02f + 3.1f;
        float zr = z * 2.02f + 7.7f;
        x = xr; z = zr;
        amp *= 0.5f;
    }
    return v / sum;
}

static float forest_mask_at(float wx, float wz)
{
    float regional = canopy_fbm(wx * 0.0009f, wz * 0.0009f, 2, 0xF0EE5Du);
    float clumps = canopy_fbm(wx * 0.0035f + 41.0f, wz * 0.0035f + 17.0f, 2, 0x1357Bu);
    float dens = regional * 0.6f + clumps * 0.4f;
    return smoothstepf(0.40f, 0.62f, dens);
}

static float smoothed_height(float x, float z, float r)
{
    if (r < 0.5f)
        return terrain_height_at(x, z);
    return terrain_height_blur_at(x, z);
}

static int gen_heightmap(void)
{
    size_t ncells = (size_t)TERRAIN_N * (size_t)TERRAIN_N;
    float *raw = (float *)malloc(ncells * sizeof(float));
    uint8_t *masked = (uint8_t *)malloc(ncells);
    long *hist = (long *)calloc(HEIGHTMAP_HIST_BUCKETS, sizeof(long));
    if (!raw || !masked || !hist)
    {
        plat_log("terrain: heightmap scratch allocation failed");
        free(raw); free(masked); free(hist);
        return -1;
    }

    float rawMin = 1e9f, rawMax = -1e9f;
    for (int j = 0; j < TERRAIN_N; j++)
    {
        float v = (float)j / (float)(TERRAIN_N - 1);
        for (int i = 0; i < TERRAIN_N; i++)
        {
            float u = (float)i / (float)(TERRAIN_N - 1);
            float x = u * 9.0f, z = v * 9.0f;

            float wx = fbm2(x * 0.22f + 40.0f, z * 0.22f + 11.0f, 4, 5001u) * 2.0f - 1.0f;
            float wz = fbm2(x * 0.22f + 91.0f, z * 0.22f + 63.0f, 4, 6002u) * 2.0f - 1.0f;
            float xw = x + wx * HEIGHTMAP_WARP_AMP;
            float zw = z + wz * HEIGHTMAP_WARP_AMP;

            float base = fbm2(xw, zw, 6, 1337u);
            float ridgeAmp = 0.12f + (0.85f - 0.12f) * smoothstepf(0.35f, 0.75f, base);
            float rid = ridged2(xw * 0.55f + 3.7f, zw * 0.55f + 7.1f, 5, 1337u * 3u + 11u);
            float h = base * 0.5f + rid * ridgeAmp;

            float dx = u - 0.5f, dz = v - 0.5f;
            float dist = sqrtf(dx * dx + dz * dz) * 2.0f;
            float t = smoothstepf(HEIGHTMAP_MASK_START, HEIGHTMAP_MASK_END, dist);
            size_t idx = (size_t)j * TERRAIN_N + i;
            masked[idx] = t > 0.001f;
            h *= 1.0f - t;
            raw[idx] = h;
            if (!masked[idx])
            {
                if (h < rawMin) rawMin = h;
                if (h > rawMax) rawMax = h;
            }
        }
    }

    float range = rawMax - rawMin;
    if (range < 1e-6f) range = 1e-6f;
    long unmaskedTotal = 0;
    for (size_t k = 0; k < ncells; k++)
    {
        if (masked[k]) continue;
        int b = (int)((raw[k] - rawMin) / range * (float)(HEIGHTMAP_HIST_BUCKETS - 1));
        if (b < 0) b = 0;
        if (b >= HEIGHTMAP_HIST_BUCKETS) b = HEIGHTMAP_HIST_BUCKETS - 1;
        hist[b]++;
        unmaskedTotal++;
    }
    long targetCount = (long)(HEIGHTMAP_CORE_FRAC * (double)unmaskedTotal);
    long acc = 0;
    int thBucket = HEIGHTMAP_HIST_BUCKETS - 1;
    for (int b = 0; b < HEIGHTMAP_HIST_BUCKETS; b++)
    {
        acc += hist[b];
        if (acc >= targetCount) { thBucket = b; break; }
    }
    float thNorm = (float)thBucket / (float)(HEIGHTMAP_HIST_BUCKETS - 1);
    if (thNorm < 1e-4f) thNorm = 1e-4f;

    float waterNorm = WATER_LEVEL / TERRAIN_HEIGHT;
    for (size_t k = 0; k < ncells; k++)
    {
        float t = (raw[k] - rawMin) / range;
        float h;
        if (t < thNorm)
        {
            float valleyT = t / thNorm;
            float shoreSoft = smoothstepf(0.0f, 1.0f, valleyT);
            h = waterNorm * (0.40f + 0.58f * shoreSoft);
        }
        else
        {
            float landT = (t - thNorm) / (1.0f - thNorm);
            h = waterNorm + (1.0f - waterNorm) * landT;
        }
        g_hm[k] = (uint16_t)(clamp01(h) * 65535.0f + 0.5f);
    }

    free(raw);
    free(masked);
    free(hist);
    return 0;
}

static void compute_chunk_bounds(void)
{
    int per = TERRAIN_N / TERRAIN_CHUNKS;
    for (int cz = 0; cz < TERRAIN_CHUNKS; cz++)
    {
        for (int cx = 0; cx < TERRAIN_CHUNKS; cx++)
        {
            uint16_t mn = 65535, mx = 0;
            int i0 = cx * per, j0 = cz * per;
            int i1 = i0 + per, j1 = j0 + per;
            if (i1 > TERRAIN_N - 1) i1 = TERRAIN_N - 1;
            if (j1 > TERRAIN_N - 1) j1 = TERRAIN_N - 1;
            for (int j = j0; j <= j1; j++)
            {
                for (int i = i0; i <= i1; i++)
                {
                    uint16_t h = g_hm[j * TERRAIN_N + i];
                    if (h < mn) mn = h;
                    if (h > mx) mx = h;
                }
            }
            int ci = cz * TERRAIN_CHUNKS + cx;
            g_cmin[ci] = (float)mn * (TERRAIN_HEIGHT / 65535.0f) - 2.0f;
            g_cmax[ci] = (float)mx * (TERRAIN_HEIGHT / 65535.0f) + 2.0f;
        }
    }
}

static float sample_height_field(const uint16_t *field, float x, float z)
{
    float u = x / TERRAIN_SCALE + 0.5f;
    float v = z / TERRAIN_SCALE + 0.5f;
    float fx = u * (float)TERRAIN_N - 0.5f;
    float fz = v * (float)TERRAIN_N - 0.5f;
    float ffx = floorf(fx), ffz = floorf(fz);
    int x0 = (int)ffx, z0 = (int)ffz;
    float tx = fx - ffx, tz = fz - ffz;
    int x1 = x0 + 1, z1 = z0 + 1;
    if (x0 < 0) x0 = 0; if (x0 > TERRAIN_N - 1) x0 = TERRAIN_N - 1;
    if (x1 < 0) x1 = 0; if (x1 > TERRAIN_N - 1) x1 = TERRAIN_N - 1;
    if (z0 < 0) z0 = 0; if (z0 > TERRAIN_N - 1) z0 = TERRAIN_N - 1;
    if (z1 < 0) z1 = 0; if (z1 > TERRAIN_N - 1) z1 = TERRAIN_N - 1;
    float a = (float)field[z0 * TERRAIN_N + x0];
    float b = (float)field[z0 * TERRAIN_N + x1];
    float c = (float)field[z1 * TERRAIN_N + x0];
    float d = (float)field[z1 * TERRAIN_N + x1];
    float m0 = a + (b - a) * tx;
    float m1 = c + (d - c) * tx;
    return (m0 + (m1 - m0) * tz) * (TERRAIN_HEIGHT / 65535.0f);
}

float terrain_height_at(float x, float z)
{
    return sample_height_field(g_hm, x, z);
}

static float terrain_height_blur_at(float x, float z)
{
    return sample_height_field(g_hm_blur, x, z);
}

float terrain_height_smooth_at(float x, float z)
{
    return terrain_height_blur_at(x, z);
}

static void build_height_blur(void)
{
    size_t n = (size_t)TERRAIN_N * (size_t)TERRAIN_N;
    float *tmp = (float *)malloc(n * sizeof(float));
    if (!tmp)
    {
        plat_log("terrain: blur scratch allocation failed");
        memcpy(g_hm_blur, g_hm, n * sizeof(uint16_t));
        return;
    }
    const int R = 6;
    for (int j = 0; j < TERRAIN_N; j++)
    {
        int rowoff = j * TERRAIN_N;
        float sum = 0.0f;
        for (int k = -R; k <= R; k++)
        {
            int xi = k; if (xi < 0) xi = 0; if (xi > TERRAIN_N - 1) xi = TERRAIN_N - 1;
            sum += (float)g_hm[rowoff + xi];
        }
        tmp[rowoff] = sum / (float)(2 * R + 1);
        for (int i = 1; i < TERRAIN_N; i++)
        {
            int addx = i + R; if (addx > TERRAIN_N - 1) addx = TERRAIN_N - 1;
            int subx = i - R - 1; if (subx < 0) subx = 0;
            sum += (float)g_hm[rowoff + addx] - (float)g_hm[rowoff + subx];
            tmp[rowoff + i] = sum / (float)(2 * R + 1);
        }
    }
    for (int i = 0; i < TERRAIN_N; i++)
    {
        float sum = 0.0f;
        for (int k = -R; k <= R; k++)
        {
            int zj = k; if (zj < 0) zj = 0; if (zj > TERRAIN_N - 1) zj = TERRAIN_N - 1;
            sum += tmp[zj * TERRAIN_N + i];
        }
        g_hm_blur[i] = (uint16_t)(sum / (float)(2 * R + 1) + 0.5f);
        for (int j = 1; j < TERRAIN_N; j++)
        {
            int addz = j + R; if (addz > TERRAIN_N - 1) addz = TERRAIN_N - 1;
            int subz = j - R - 1; if (subz < 0) subz = 0;
            sum += tmp[addz * TERRAIN_N + i] - tmp[subz * TERRAIN_N + i];
            g_hm_blur[j * TERRAIN_N + i] = (uint16_t)(sum / (float)(2 * R + 1) + 0.5f);
        }
    }
    free(tmp);
}

const uint16_t *terrain_heightmap(void)
{
    return g_hm;
}

VImg *terrain_heightmap_tex(void)
{
    return &g_hmtex;
}

VImg *terrain_lightmap_tex(void)
{
    return &g_lmtex;
}

static float bake_shadow(v3 p, v3 sun)
{
    float shadow = 1.0f;
    float t = 4.0f;
    float step = 4.0f;
    for (int i = 0; i < 48; i++)
    {
        v3 q = v3add(p, v3scale(sun, t));
        float r = t * 0.035f;
        float gh = smoothed_height(q.x, q.z, r);
        float clearance = q.y - gh - 1.0f;
        float w = 0.006f * t + 0.45f;
        if (w > 15.0f) w = 15.0f;
        float pen = smoothstepf(-w, w, clearance);
        if (pen < shadow) shadow = pen;
        if (shadow <= 0.015f) break;
        t += step;
        step *= 1.10f;
    }
    return clamp01(shadow);
}

static float bake_ao(v3 p)
{
    float sum = 0.0f;
    for (int a = 0; a < 8; a++)
    {
        float ang = (float)a * (6.28318530718f / 8.0f);
        float dx = cosf(ang), dz = sinf(ang);
        float maxSlope = 0.0f;
        float t = 3.0f;
        float step = 3.0f;
        for (int s = 0; s < 8; s++)
        {
            float gh = smoothed_height(p.x + dx * t, p.z + dz * t, t * 0.06f);
            float slope = (gh - p.y - 1.0f) / t;
            if (slope > maxSlope) maxSlope = slope;
            if (maxSlope * 1.5f >= 1.0f) break;
            t += step;
            step *= 1.25f;
        }
        sum += 1.0f - clamp01(maxSlope * 1.5f);
    }
    return clamp01(sum / 8.0f);
}

static bool forest_eligible(float h)
{
    float hn = h / TERRAIN_HEIGHT;
    if (hn < 0.09f || hn > 0.56f) return false;
    if (h < WATER_LEVEL + 1.5f) return false;
    return true;
}

static void bake_lightmap(void)
{
    double t0 = plat_time();
    v3 sun = v3norm((v3){ 0.45f, 0.30f, 0.55f });
    v3 sunxz = v3norm((v3){ sun.x, 0.0f, sun.z });
    const float canopyReach = 17.0f;
    for (int j = 0; j < LIGHTMAP_N; j++)
    {
        float v = ((float)j + 0.5f) / (float)LIGHTMAP_N;
        float z = (v - 0.5f) * TERRAIN_SCALE;
        for (int i = 0; i < LIGHTMAP_N; i++)
        {
            float u = ((float)i + 0.5f) / (float)LIGHTMAP_N;
            float x = (u - 0.5f) * TERRAIN_SCALE;
            float h = terrain_height_at(x, z);
            v3 p = { x, h + 0.5f, z };
            float shadow = bake_shadow(p, sun);
            float ao = bake_ao(p);

            float sx = x + sunxz.x * canopyReach;
            float sz = z + sunxz.z * canopyReach;
            float sh = terrain_height_at(sx, sz);
            float canopySrc = forest_eligible(sh) ? forest_mask_at(sx, sz) : 0.0f;
            shadow *= 1.0f - 0.72f * canopySrc;

            float canopyHere = forest_eligible(h) ? forest_mask_at(x, z) : 0.0f;
            ao *= 1.0f - 0.28f * canopyHere;

            uint8_t *px = g_lightmap + (size_t)(j * LIGHTMAP_N + i) * 4;
            px[0] = (uint8_t)(clamp01(shadow) * 255.0f + 0.5f);
            px[1] = (uint8_t)(clamp01(ao) * 255.0f + 0.5f);
            px[2] = (uint8_t)(canopyHere * 255.0f + 0.5f);
            px[3] = 255;
        }
    }
    double dt = plat_time() - t0;
    plat_log("terrain: lightmap bake took %.3fs", dt);
}

static void planes_from(const m4 *vp, Plane *pl)
{
    const float *m = vp->m;
    float r0[4] = { m[0], m[4], m[8], m[12] };
    float r1[4] = { m[1], m[5], m[9], m[13] };
    float r2[4] = { m[2], m[6], m[10], m[14] };
    float r3[4] = { m[3], m[7], m[11], m[15] };
    pl[0] = (Plane){ r3[0] + r0[0], r3[1] + r0[1], r3[2] + r0[2], r3[3] + r0[3] };
    pl[1] = (Plane){ r3[0] - r0[0], r3[1] - r0[1], r3[2] - r0[2], r3[3] - r0[3] };
    pl[2] = (Plane){ r3[0] + r1[0], r3[1] + r1[1], r3[2] + r1[2], r3[3] + r1[3] };
    pl[3] = (Plane){ r3[0] - r1[0], r3[1] - r1[1], r3[2] - r1[2], r3[3] - r1[3] };
    pl[4] = (Plane){ r2[0], r2[1], r2[2], r2[3] };
    pl[5] = (Plane){ r3[0] - r2[0], r3[1] - r2[1], r3[2] - r2[2], r3[3] - r2[3] };
}

static bool aabb_visible(const Plane *pl, v3 mn, v3 mx)
{
    for (int i = 0; i < 6; i++)
    {
        float px = pl[i].a > 0.0f ? mx.x : mn.x;
        float py = pl[i].b > 0.0f ? mx.y : mn.y;
        float pz = pl[i].c > 0.0f ? mx.z : mn.z;
        if (pl[i].a * px + pl[i].b * py + pl[i].c * pz + pl[i].d < 0.0f) return false;
    }
    return true;
}

static VkPipelineShaderStageCreateInfo stage_info(VkShaderStageFlagBits s, VkShaderModule m)
{
    return (VkPipelineShaderStageCreateInfo){
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = s,
        .module = m,
        .pName = "main"
    };
}

static int create_pipeline(VkCore *c, VkRenderPass rp, bool multiview)
{
    const char *vs_name = g_tess
        ? (multiview ? "shaders/terrain.vert.mv.spv" : "shaders/terrain.vert.spv")
        : (multiview ? "shaders/terrain_static.vert.mv.spv" : "shaders/terrain_static.vert.spv");
    VkShaderModule vs = vkc_shader(c, vs_name);
    VkShaderModule fs = vkc_shader(c, multiview ? "shaders/terrain.frag.mv.spv" : "shaders/terrain.frag.spv");
    VkShaderModule tc = VK_NULL_HANDLE, te = VK_NULL_HANDLE;
    if (g_tess)
    {
        tc = vkc_shader(c, multiview ? "shaders/terrain.tesc.mv.spv" : "shaders/terrain.tesc.spv");
        te = vkc_shader(c, multiview ? "shaders/terrain.tese.mv.spv" : "shaders/terrain.tese.spv");
    }
    bool ok = vs != VK_NULL_HANDLE && fs != VK_NULL_HANDLE &&
              (!g_tess || (tc != VK_NULL_HANDLE && te != VK_NULL_HANDLE));
    VkResult r = VK_ERROR_INITIALIZATION_FAILED;
    if (ok)
    {
        VkPipelineShaderStageCreateInfo stages[4];
        uint32_t nstages = 0;
        stages[nstages++] = stage_info(VK_SHADER_STAGE_VERTEX_BIT, vs);
        if (g_tess)
        {
            stages[nstages++] = stage_info(VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, tc);
            stages[nstages++] = stage_info(VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, te);
        }
        stages[nstages++] = stage_info(VK_SHADER_STAGE_FRAGMENT_BIT, fs);
        for (uint32_t si = 0; si < nstages; si++)
            stages[si].pSpecializationInfo = vkc_tier_spec(c);
        VkPipelineVertexInputStateCreateInfo vin = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO
        };
        VkPipelineInputAssemblyStateCreateInfo ia = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .topology = g_tess ? VK_PRIMITIVE_TOPOLOGY_PATCH_LIST : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
        };
        VkPipelineTessellationStateCreateInfo ts = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO,
            .patchControlPoints = 4
        };
        VkPipelineViewportStateCreateInfo vps = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .viewportCount = 1,
            .scissorCount = 1
        };
        VkPipelineRasterizationStateCreateInfo rs = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .polygonMode = VK_POLYGON_MODE_FILL,
            .cullMode = VK_CULL_MODE_NONE,
            .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
            .lineWidth = 1.0f
        };
        VkPipelineMultisampleStateCreateInfo ms = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .rasterizationSamples = c->msaa
        };
        VkPipelineDepthStencilStateCreateInfo ds = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .depthTestEnable = VK_TRUE,
            .depthWriteEnable = VK_TRUE,
            .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL
        };
        VkPipelineColorBlendAttachmentState cba = {
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
        };
        VkPipelineColorBlendStateCreateInfo cb = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .attachmentCount = 1,
            .pAttachments = &cba
        };
        VkDynamicState dyn[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dsi = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .dynamicStateCount = 2,
            .pDynamicStates = dyn
        };
        VkGraphicsPipelineCreateInfo gp = {
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .stageCount = nstages,
            .pStages = stages,
            .pVertexInputState = &vin,
            .pInputAssemblyState = &ia,
            .pTessellationState = g_tess ? &ts : NULL,
            .pViewportState = &vps,
            .pRasterizationState = &rs,
            .pMultisampleState = &ms,
            .pDepthStencilState = &ds,
            .pColorBlendState = &cb,
            .pDynamicState = &dsi,
            .layout = g_layout,
            .renderPass = rp,
            .subpass = 0
        };
        r = vkCreateGraphicsPipelines(c->device, c->pcache, 1, &gp, NULL, &g_pipe);
    }
    if (vs != VK_NULL_HANDLE) vkDestroyShaderModule(c->device, vs, NULL);
    if (fs != VK_NULL_HANDLE) vkDestroyShaderModule(c->device, fs, NULL);
    if (tc != VK_NULL_HANDLE) vkDestroyShaderModule(c->device, tc, NULL);
    if (te != VK_NULL_HANDLE) vkDestroyShaderModule(c->device, te, NULL);
    if (r != VK_SUCCESS)
    {
        plat_log("terrain: pipeline creation failed (%d)", (int)r);
        return -1;
    }
    return 0;
}

int terrain_init(VkCore *c, VkRenderPass rp, bool multiview, const Scene *scene)
{
    g_core = c;
    g_layout = scene->pipe_layout;
    g_tess = c->has_tess;
    if (gen_heightmap() != 0)
        return -1;
    compute_chunk_bounds();
    build_height_blur();
    if (vkc_texture_r16(c, g_hm, TERRAIN_N, TERRAIN_N, &g_hmtex) != 0)
    {
        plat_log("terrain: heightmap upload failed");
        return -1;
    }
    bake_lightmap();
    if (vkc_texture_rgba(c, g_lightmap, LIGHTMAP_N, LIGHTMAP_N, false, false, &g_lmtex) != 0)
    {
        plat_log("terrain: lightmap upload failed");
        return -1;
    }
    if (!g_tess)
    {
        if (vkc_buffer(c, STATIC_IDX * sizeof(uint16_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                       &g_ib) != 0)
        {
            plat_log("terrain: index buffer creation failed");
            return -1;
        }
        void *map = g_ib.map;
        if (!map)
        {
            VkResult mr = vkMapMemory(c->device, g_ib.mem, 0, VK_WHOLE_SIZE, 0, &map);
            if (mr != VK_SUCCESS)
            {
                plat_log("terrain: index buffer map failed (%d)", (int)mr);
                return -1;
            }
        }
        uint16_t *idx = (uint16_t *)map;
        uint32_t k = 0;
        for (int z = 0; z < STATIC_GRID; z++)
        {
            for (int x = 0; x < STATIC_GRID; x++)
            {
                uint16_t i0 = (uint16_t)(z * (STATIC_GRID + 1) + x);
                uint16_t i1 = (uint16_t)(i0 + 1);
                uint16_t i2 = (uint16_t)(i0 + STATIC_GRID + 1);
                uint16_t i3 = (uint16_t)(i2 + 1);
                idx[k++] = i0; idx[k++] = i2; idx[k++] = i1;
                idx[k++] = i1; idx[k++] = i2; idx[k++] = i3;
            }
        }
        if (!g_ib.map) vkUnmapMemory(c->device, g_ib.mem);
    }
    return create_pipeline(c, rp, multiview);
}

void terrain_record(VkCommandBuffer cmd, uint32_t slot, const FrameUBO *ubo)
{
    Plane pl[2][6];
    uint32_t nviews = g_scene.multiview ? 2 : 1;
    for (uint32_t v = 0; v < nviews; v++)
        planes_from(&ubo->viewproj[v], pl[v]);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_layout, 0, 1,
                            &g_scene.set0[slot], 0, NULL);
    if (!g_tess) vkCmdBindIndexBuffer(cmd, g_ib.buf, 0, VK_INDEX_TYPE_UINT16);
    float cs = TERRAIN_SCALE / (float)TERRAIN_CHUNKS;
    for (int cz = 0; cz < TERRAIN_CHUNKS; cz++)
    {
        for (int cx = 0; cx < TERRAIN_CHUNKS; cx++)
        {
            int ci = cz * TERRAIN_CHUNKS + cx;
            float x0 = -TERRAIN_SCALE * 0.5f + (float)cx * cs;
            float z0 = -TERRAIN_SCALE * 0.5f + (float)cz * cs;
            v3 mn = { x0, g_cmin[ci], z0 };
            v3 mx = { x0 + cs, g_cmax[ci], z0 + cs };
            bool vis = false;
            for (uint32_t v = 0; v < nviews && !vis; v++)
                vis = aabb_visible(pl[v], mn, mx);
            if (!vis) continue;
            TerrainPC pc = { x0, z0, cs, 0.0f };
            vkCmdPushConstants(cmd, g_layout, TERRAIN_PC_STAGES, 0, sizeof pc, &pc);
            if (g_tess) vkCmdDraw(cmd, 4, 16, 0, 0);
            else vkCmdDrawIndexed(cmd, STATIC_IDX, 1, 0, 0, 0);
        }
    }
}

void terrain_destroy(void)
{
    if (!g_core) return;
    if (g_pipe != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(g_core->device, g_pipe, NULL);
        g_pipe = VK_NULL_HANDLE;
    }
    if (g_ib.buf != VK_NULL_HANDLE) vkc_buffer_destroy(g_core, &g_ib);
    if (g_hmtex.img != VK_NULL_HANDLE) vkc_image_destroy(g_core, &g_hmtex);
    if (g_lmtex.img != VK_NULL_HANDLE) vkc_image_destroy(g_core, &g_lmtex);
    g_core = NULL;
}
