#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <limits.h>
#include <libgen.h>
#include <mach-o/dyld.h>
#include "vista.h"

static char g_exedir[PATH_MAX];

static void exedir_init(void)
{
    char buf[PATH_MAX];
    uint32_t n = sizeof buf;
    if (_NSGetExecutablePath(buf, &n) != 0) {
        g_exedir[0] = '.';
        g_exedir[1] = 0;
        return;
    }
    char real[PATH_MAX];
    if (!realpath(buf, real)) {
        g_exedir[0] = '.';
        g_exedir[1] = 0;
        return;
    }
    snprintf(g_exedir, sizeof g_exedir, "%s", dirname(real));
}

uint8_t *plat_load_asset(const char *name, int *out_size)
{
    static const char *prefixes[4] = { "", "../", "assets/", "../assets/" };
    for (int i = 0; i < 4; i++) {
        char path[PATH_MAX * 2];
        snprintf(path, sizeof path, "%s/%s%s", g_exedir, prefixes[i], name);
        FILE *f = fopen(path, "rb");
        if (!f)
            continue;
        if (fseek(f, 0, SEEK_END) != 0) {
            fclose(f);
            continue;
        }
        long sz = ftell(f);
        if (sz < 0) {
            fclose(f);
            continue;
        }
        if (fseek(f, 0, SEEK_SET) != 0) {
            fclose(f);
            continue;
        }
        uint8_t *buf = (uint8_t *)malloc((size_t)sz + 1);
        if (!buf) {
            fclose(f);
            return NULL;
        }
        if (sz > 0 && fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
            free(buf);
            fclose(f);
            continue;
        }
        fclose(f);
        buf[sz] = 0;
        if (out_size)
            *out_size = (int)sz;
        return buf;
    }
    plat_log("asset not found: %s\n", name);
    if (out_size)
        *out_size = 0;
    return NULL;
}

void plat_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    fflush(stdout);
}

double plat_time(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

#define HEADLESS_W 1600
#define HEADLESS_H 900

int main(void)
{
    exedir_init();
    VkCore core;
    memset(&core, 0, sizeof core);
    if (vkc_create_instance(&core, NULL, 0) != 0) {
        plat_log("vkc_create_instance failed");
        return 1;
    }
    if (vkc_pick_device(&core, VK_NULL_HANDLE) != 0) {
        plat_log("vkc_pick_device failed");
        return 1;
    }
    plat_log("headless device: %s", core.props.deviceName);
    if (vkc_create_device(&core, NULL, 0) != 0) {
        plat_log("vkc_create_device failed");
        return 1;
    }
    if (vkc_finish_setup(&core) != 0) {
        plat_log("vkc_finish_setup failed");
        return 1;
    }
    OffChain oc;
    memset(&oc, 0, sizeof oc);
    if (vkc_offchain_create(&oc, &core, HEADLESS_W, HEADLESS_H) != 0) {
        plat_log("vkc_offchain_create failed");
        return 1;
    }
    if (render_init(&core, HEADLESS_W, HEADLESS_H, 1) != 0) {
        plat_log("render_init failed");
        return 1;
    }
    if (assets_load_textures(&core) != 0) {
        plat_log("assets_load_textures failed");
        return 1;
    }
    if (scene_init(&core, render_scene_rp(), false) != 0) {
        plat_log("scene_init failed");
        return 1;
    }
    if (render_post_init(&core, oc.rt.rp) != 0) {
        plat_log("render_post_init failed");
        return 1;
    }
    game_init(&g_game);
    const char *cam_env = getenv("VISTA_CAM");
    if (cam_env) {
        float cx, cz, cyaw, cpitch, ctime;
        if (sscanf(cam_env, "%f,%f,%f,%f,%f", &cx, &cz, &cyaw, &cpitch, &ctime) == 5) {
            g_game.pos.x = cx;
            g_game.pos.z = cz;
            g_game.pos.y = terrain_height_at(cx, cz) + EYE_HEIGHT;
            g_game.yaw = cyaw;
            g_game.pitch = cpitch;
            g_game.time = ctime;
        }
    }
    int frames = 120;
    const char *frames_env = getenv("VISTA_SMOKE_FRAMES");
    if (frames_env) {
        int fv = atoi(frames_env);
        if (fv > 0 && fv < 100000)
            frames = fv;
    }
    const char *out_env = getenv("VISTA_SHOT");
    char shot_path[PATH_MAX * 2];
    if (out_env)
        snprintf(shot_path, sizeof shot_path, "%s", out_env);
    else
        snprintf(shot_path, sizeof shot_path, "%s/smoke.png", g_exedir);
    int rc = 0;
    Input in;
    memset(&in, 0, sizeof in);
    const float dt = 1.0f / 120.0f;
    for (int i = 0; i < frames; i++) {
        game_update(&g_game, &in, dt);
        FrameUBO ubo;
        game_flat_ubo(&g_game, (float)HEADLESS_W / (float)HEADLESS_H, &ubo);
        if (i == frames - 1) {
            if (vkc_offchain_screenshot(&oc, &ubo, shot_path) != 0) {
                plat_log("screenshot failed");
                rc = 1;
            } else {
                plat_log("SMOKE OK");
            }
        } else if (vkc_offchain_frame(&oc, &ubo) != 0) {
            plat_log("vkc_offchain_frame failed");
            rc = 1;
            break;
        }
    }
    scene_destroy();
    render_destroy();
    assets_destroy_textures(&core);
    vkc_offchain_destroy(&oc);
    vkc_core_destroy(&core);
    return rc;
}
