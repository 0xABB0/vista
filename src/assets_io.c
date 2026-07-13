#include "vista.h"
#include <stdio.h>
#include <stdlib.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include "stb_image.h"

Textures g_tex;

static int assets_load_one(VkCore *c, const char *id, const char *map, bool srgb, VImg *out)
{
    char path[256];
    snprintf(path, sizeof path, "tex/%s/%s_2K-JPG_%s.jpg", id, id, map);
    int size = 0;
    uint8_t *raw = plat_load_asset(path, &size);
    if (!raw || size <= 0) {
        plat_log("assets: cannot read %s", path);
        free(raw);
        return -1;
    }
    int w = 0, h = 0, n = 0;
    stbi_uc *px = stbi_load_from_memory(raw, size, &w, &h, &n, 4);
    free(raw);
    if (!px) {
        plat_log("assets: decode failed %s: %s", path, stbi_failure_reason());
        return -1;
    }
    int r = vkc_texture_rgba(c, px, (uint32_t)w, (uint32_t)h, srgb, true, out);
    stbi_image_free(px);
    if (r) {
        plat_log("assets: upload failed %s (%d)", path, r);
        return -1;
    }
    plat_log("assets: loaded %s %dx%d", path, w, h);
    return 0;
}

int assets_load_textures(VkCore *c)
{
    static const struct { const char *id; size_t color_off, normal_off; } sets[] = {
        { "Grass004", offsetof(Textures, grass_color), offsetof(Textures, grass_normal) },
        { "Rock035", offsetof(Textures, rock_color), offsetof(Textures, rock_normal) },
        { "Ground048", offsetof(Textures, dirt_color), offsetof(Textures, dirt_normal) },
    };
    for (size_t i = 0; i < sizeof sets / sizeof sets[0]; i++) {
        VImg *color = (VImg *)((uint8_t *)&g_tex + sets[i].color_off);
        VImg *normal = (VImg *)((uint8_t *)&g_tex + sets[i].normal_off);
        if (assets_load_one(c, sets[i].id, "Color", true, color)) return -1;
        if (assets_load_one(c, sets[i].id, "NormalGL", false, normal)) return -1;
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
}
