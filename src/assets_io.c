#include "vista.h"
#include <stdio.h>
#include <stdlib.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include "stb_image.h"

Textures g_tex;

static stbi_uc *assets_decode(const char *id, const char *map, int channels, int *w, int *h)
{
    char path[256];
    snprintf(path, sizeof path, "tex/%s/%s_2K-JPG_%s.jpg", id, id, map);
    int size = 0;
    uint8_t *raw = plat_load_asset(path, &size);
    if (!raw || size <= 0) {
        plat_log("assets: cannot read %s", path);
        free(raw);
        return NULL;
    }
    int n = 0;
    stbi_uc *px = stbi_load_from_memory(raw, size, w, h, &n, channels);
    free(raw);
    if (!px)
        plat_log("assets: decode failed %s: %s", path, stbi_failure_reason());
    return px;
}

static int assets_load_normal(VkCore *c, const char *id, VImg *out)
{
    int w = 0, h = 0;
    stbi_uc *px = assets_decode(id, "NormalGL", 4, &w, &h);
    if (!px) return -1;
    int r = vkc_texture_rgba(c, px, (uint32_t)w, (uint32_t)h, false, true, out);
    stbi_image_free(px);
    if (r) {
        plat_log("assets: upload failed %s normal (%d)", id, r);
        return -1;
    }
    plat_log("assets: loaded %s NormalGL %dx%d", id, w, h);
    return 0;
}

static int assets_load_color_rough(VkCore *c, const char *id, VImg *out)
{
    int w = 0, h = 0;
    stbi_uc *px = assets_decode(id, "Color", 4, &w, &h);
    if (!px) return -1;
    int rw = 0, rh = 0;
    stbi_uc *rp = assets_decode(id, "Roughness", 1, &rw, &rh);
    if (!rp) {
        stbi_image_free(px);
        return -1;
    }
    for (int y = 0; y < h; y++) {
        const stbi_uc *rrow = rp + (size_t)((int64_t)y * rh / h) * rw;
        stbi_uc *row = px + (size_t)y * w * 4;
        for (int x = 0; x < w; x++)
            row[x * 4 + 3] = rrow[(int64_t)x * rw / w];
    }
    stbi_image_free(rp);
    int r = vkc_texture_rgba(c, px, (uint32_t)w, (uint32_t)h, true, true, out);
    stbi_image_free(px);
    if (r) {
        plat_log("assets: upload failed %s color (%d)", id, r);
        return -1;
    }
    plat_log("assets: loaded %s Color+Roughness %dx%d", id, w, h);
    return 0;
}

int assets_load_textures(VkCore *c)
{
    static const struct { const char *id; size_t color_off, normal_off; } sets[] = {
        { "Grass004", offsetof(Textures, grass_color), offsetof(Textures, grass_normal) },
        { "Rock035", offsetof(Textures, rock_color), offsetof(Textures, rock_normal) },
        { "Ground048", offsetof(Textures, dirt_color), offsetof(Textures, dirt_normal) },
        { "Snow010A", offsetof(Textures, snow_color), offsetof(Textures, snow_normal) },
    };
    for (size_t i = 0; i < sizeof sets / sizeof sets[0]; i++) {
        VImg *color = (VImg *)((uint8_t *)&g_tex + sets[i].color_off);
        VImg *normal = (VImg *)((uint8_t *)&g_tex + sets[i].normal_off);
        if (assets_load_color_rough(c, sets[i].id, color)) return -1;
        if (assets_load_normal(c, sets[i].id, normal)) return -1;
    }
    return 0;
}

void assets_destroy_textures(VkCore *c)
{
    vkc_image_destroy(c, &g_tex.grass_color);
    vkc_image_destroy(c, &g_tex.grass_normal);
    vkc_image_destroy(c, &g_tex.rock_color);
    vkc_image_destroy(c, &g_tex.rock_normal);
    vkc_image_destroy(c, &g_tex.dirt_color);
    vkc_image_destroy(c, &g_tex.dirt_normal);
    vkc_image_destroy(c, &g_tex.snow_color);
    vkc_image_destroy(c, &g_tex.snow_normal);
}
