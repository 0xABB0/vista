#include "vista.h"
#include "android_native_app_glue.h"
#include <android/log.h>
#include <android/asset_manager.h>
#include <android/native_window.h>
#include <android/input.h>
#include <android/keycodes.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

extern struct android_app *gapp;

static VkCore s_core;
static int s_focused;
static int s_game_inited;

#ifdef VISTA_VR

static int s_xr_ready;

#else

static FlatChain s_fc;
static VkSurfaceKHR s_surface;
static int s_core_ready;
static int s_chain_ready;
static int s_scene_ready;
static double s_last_time;
static float s_win_w = 1080.0f;
static float s_win_h = 1920.0f;

static struct { int active; int32_t id; float ax, ay, cx, cy; } s_stick;
static struct { int active; int32_t id; float lx, ly; } s_look;
static float s_look_dx, s_look_dy;

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

#endif

void plat_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    __android_log_vprint(ANDROID_LOG_INFO, "vista", fmt, ap);
    va_end(ap);
}

double plat_time(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

uint8_t *plat_load_asset(const char *name, int *out_size)
{
    if (out_size) *out_size = 0;
    if (!gapp || !gapp->activity || !gapp->activity->assetManager) return NULL;
    AAsset *a = AAssetManager_open(gapp->activity->assetManager, name, AASSET_MODE_BUFFER);
    if (!a) return NULL;
    off_t len = AAsset_getLength(a);
    if (len <= 0)
    {
        AAsset_close(a);
        return NULL;
    }
    uint8_t *buf = (uint8_t *)malloc((size_t)len);
    if (!buf)
    {
        AAsset_close(a);
        return NULL;
    }
    off_t got = 0;
    while (got < len)
    {
        int r = AAsset_read(a, buf + got, (size_t)(len - got));
        if (r <= 0)
        {
            free(buf);
            AAsset_close(a);
            return NULL;
        }
        got += r;
    }
    AAsset_close(a);
    if (out_size) *out_size = (int)len;
    return buf;
}

static void plat_fail(const char *what)
{
    plat_log("fatal: %s", what);
    if (gapp && gapp->activity) ANativeActivity_finish(gapp->activity);
}

#ifndef VISTA_VR

static void flat_term_window(void)
{
    if (!s_chain_ready) return;
    VkResult r = vkDeviceWaitIdle(s_core.device);
    if (r != VK_SUCCESS) plat_log("vkDeviceWaitIdle failed: %d", (int)r);
    vkc_flatchain_destroy(&s_fc);
    if (s_surface != VK_NULL_HANDLE)
    {
        vkDestroySurfaceKHR(s_core.instance, s_surface, NULL);
        s_surface = VK_NULL_HANDLE;
    }
    s_chain_ready = 0;
}

static void flat_init_window(void)
{
    if (!gapp->window) return;
    if (s_chain_ready) flat_term_window();
    if (!s_core_ready)
    {
        static const char *iext[] = { VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_ANDROID_SURFACE_EXTENSION_NAME };
        static const char *dext[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
        if (vkc_create_instance(&s_core, iext, 2)) { plat_fail("vkc_create_instance"); return; }
        if (vkc_pick_device(&s_core, VK_NULL_HANDLE)) { plat_fail("vkc_pick_device"); return; }
        if (vkc_create_device(&s_core, dext, 1)) { plat_fail("vkc_create_device"); return; }
        if (vkc_finish_setup(&s_core)) { plat_fail("vkc_finish_setup"); return; }
        s_core_ready = 1;
    }
    VkAndroidSurfaceCreateInfoKHR sci = {
        .sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR,
        .window = gapp->window,
    };
    VkResult r = vkCreateAndroidSurfaceKHR(s_core.instance, &sci, NULL, &s_surface);
    if (r != VK_SUCCESS)
    {
        plat_log("vkCreateAndroidSurfaceKHR failed: %d", (int)r);
        plat_fail("surface");
        return;
    }
    int32_t ww = ANativeWindow_getWidth(gapp->window);
    int32_t wh = ANativeWindow_getHeight(gapp->window);
    if (ww > 0) s_win_w = (float)ww;
    if (wh > 0) s_win_h = (float)wh;
    if (vkc_flatchain_create(&s_fc, &s_core, s_surface, (uint32_t)s_win_w, (uint32_t)s_win_h))
    {
        vkDestroySurfaceKHR(s_core.instance, s_surface, NULL);
        s_surface = VK_NULL_HANDLE;
        plat_fail("vkc_flatchain_create");
        return;
    }
    s_chain_ready = 1;
    if (!s_scene_ready)
    {
        if (render_init(&s_core, s_fc.rt.width, s_fc.rt.height, 1)) { plat_fail("render_init"); return; }
        if (assets_load_textures(&s_core)) { plat_fail("assets_load_textures"); return; }
        if (scene_init(&s_core, render_scene_rp(), false)) { plat_fail("scene_init"); return; }
        if (render_post_init(&s_core, s_fc.rt.rp)) { plat_fail("render_post_init"); return; }
        s_scene_ready = 1;
    }
    else if (render_resize(s_fc.rt.width, s_fc.rt.height))
    {
        plat_fail("render_resize");
        return;
    }
    s_last_time = plat_time();
}

static void flat_recreate(void)
{
    VkResult r = vkDeviceWaitIdle(s_core.device);
    if (r != VK_SUCCESS) plat_log("vkDeviceWaitIdle failed: %d", (int)r);
    vkc_flatchain_destroy(&s_fc);
    s_chain_ready = 0;
    if (!gapp->window)
    {
        if (s_surface != VK_NULL_HANDLE)
        {
            vkDestroySurfaceKHR(s_core.instance, s_surface, NULL);
            s_surface = VK_NULL_HANDLE;
        }
        return;
    }
    int32_t ww = ANativeWindow_getWidth(gapp->window);
    int32_t wh = ANativeWindow_getHeight(gapp->window);
    if (ww > 0) s_win_w = (float)ww;
    if (wh > 0) s_win_h = (float)wh;
    if (vkc_flatchain_create(&s_fc, &s_core, s_surface, (uint32_t)s_win_w, (uint32_t)s_win_h))
    {
        vkDestroySurfaceKHR(s_core.instance, s_surface, NULL);
        s_surface = VK_NULL_HANDLE;
        plat_log("swapchain recreate failed");
        return;
    }
    s_chain_ready = 1;
}

static void flat_frame(void)
{
    if (!s_chain_ready || !s_scene_ready || !s_focused) return;
    double now = plat_time();
    float dt = (float)(now - s_last_time);
    s_last_time = now;
    dt = clampf(dt, 0.0f, 0.1f);
    Input in = { 0 };
    if (s_stick.active)
    {
        float rad = s_win_w * 0.15f;
        if (rad < 1.0f) rad = 1.0f;
        in.move_x = clampf((s_stick.cx - s_stick.ax) / rad, -1.0f, 1.0f);
        in.move_y = clampf(-(s_stick.cy - s_stick.ay) / rad, -1.0f, 1.0f);
    }
    in.look_dx = s_look_dx;
    in.look_dy = s_look_dy;
    s_look_dx = 0.0f;
    s_look_dy = 0.0f;
    game_update(&g_game, &in, dt);
    FrameUBO ubo;
    game_flat_ubo(&g_game, s_win_w / s_win_h, &ubo);
    if (vkc_flatchain_frame(&s_fc, &ubo)) flat_recreate();
}

static int32_t on_input(struct android_app *app, AInputEvent *ev)
{
    (void)app;
    int32_t type = AInputEvent_getType(ev);
    if (type == AINPUT_EVENT_TYPE_KEY)
    {
        if (AKeyEvent_getKeyCode(ev) == AKEYCODE_BACK)
        {
            if (AKeyEvent_getAction(ev) == AKEY_EVENT_ACTION_UP)
                ANativeActivity_finish(gapp->activity);
            return 1;
        }
        return 0;
    }
    if (type != AINPUT_EVENT_TYPE_MOTION) return 0;
    int32_t action = AMotionEvent_getAction(ev);
    int32_t act = action & AMOTION_EVENT_ACTION_MASK;
    size_t pidx = (size_t)((action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
    size_t count = AMotionEvent_getPointerCount(ev);
    switch (act)
    {
    case AMOTION_EVENT_ACTION_DOWN:
    case AMOTION_EVENT_ACTION_POINTER_DOWN:
    {
        if (pidx >= count) break;
        int32_t id = AMotionEvent_getPointerId(ev, pidx);
        float x = AMotionEvent_getX(ev, pidx);
        float y = AMotionEvent_getY(ev, pidx);
        if (x < s_win_w * 0.5f)
        {
            if (!s_stick.active)
            {
                s_stick.active = 1;
                s_stick.id = id;
                s_stick.ax = x;
                s_stick.ay = y;
                s_stick.cx = x;
                s_stick.cy = y;
            }
        }
        else if (!s_look.active)
        {
            s_look.active = 1;
            s_look.id = id;
            s_look.lx = x;
            s_look.ly = y;
        }
        break;
    }
    case AMOTION_EVENT_ACTION_MOVE:
        for (size_t i = 0; i < count; i++)
        {
            int32_t id = AMotionEvent_getPointerId(ev, i);
            float x = AMotionEvent_getX(ev, i);
            float y = AMotionEvent_getY(ev, i);
            if (s_stick.active && id == s_stick.id)
            {
                s_stick.cx = x;
                s_stick.cy = y;
            }
            else if (s_look.active && id == s_look.id)
            {
                s_look_dx += x - s_look.lx;
                s_look_dy += y - s_look.ly;
                s_look.lx = x;
                s_look.ly = y;
            }
        }
        break;
    case AMOTION_EVENT_ACTION_UP:
    case AMOTION_EVENT_ACTION_POINTER_UP:
    {
        if (pidx >= count) break;
        int32_t id = AMotionEvent_getPointerId(ev, pidx);
        if (s_stick.active && id == s_stick.id) s_stick.active = 0;
        if (s_look.active && id == s_look.id) s_look.active = 0;
        break;
    }
    case AMOTION_EVENT_ACTION_CANCEL:
        s_stick.active = 0;
        s_look.active = 0;
        break;
    default:
        break;
    }
    return 1;
}

#else

static void vr_frame(void)
{
    if (!s_xr_ready) return;
    Input in = { 0 };
    float dt = 0.0f;
    if (!xr_running() || xr_frame(&g_game, &in, &dt) || in.quit || !xr_running())
        ANativeActivity_finish(gapp->activity);
}

static int32_t on_input(struct android_app *app, AInputEvent *ev)
{
    (void)app;
    if (AInputEvent_getType(ev) == AINPUT_EVENT_TYPE_KEY &&
        AKeyEvent_getKeyCode(ev) == AKEYCODE_BACK)
    {
        if (AKeyEvent_getAction(ev) == AKEY_EVENT_ACTION_UP)
            ANativeActivity_finish(gapp->activity);
        return 1;
    }
    return 0;
}

#endif

static void on_cmd(struct android_app *app, int32_t cmd)
{
    (void)app;
    switch (cmd)
    {
    case APP_CMD_INIT_WINDOW:
#ifdef VISTA_VR
        if (!s_xr_ready)
        {
            if (xr_create(&s_core))
            {
                xr_shutdown();
                plat_fail("xr_create");
            }
            else s_xr_ready = 1;
        }
#else
        flat_init_window();
#endif
        break;
    case APP_CMD_TERM_WINDOW:
#ifndef VISTA_VR
        flat_term_window();
#endif
        break;
    case APP_CMD_GAINED_FOCUS:
        s_focused = 1;
#ifndef VISTA_VR
        s_last_time = plat_time();
#endif
        break;
    case APP_CMD_LOST_FOCUS:
        s_focused = 0;
        break;
    default:
        break;
    }
}

void android_main(struct android_app *app)
{
    gapp = app;
    app->onAppCmd = on_cmd;
    app->onInputEvent = on_input;
    if (!s_game_inited)
    {
        game_init(&g_game);
        s_game_inited = 1;
    }
    while (!app->destroyRequested)
    {
#ifdef VISTA_VR
        int timeout = (s_xr_ready && xr_running()) ? 0 : -1;
#else
        int timeout = (s_chain_ready && s_focused) ? 0 : -1;
#endif
        int events;
        struct android_poll_source *src;
        while (ALooper_pollOnce(timeout, NULL, &events, (void **)&src) >= 0)
        {
            if (src) src->process(app, src);
            if (app->destroyRequested) break;
            timeout = 0;
        }
        if (app->destroyRequested) break;
#ifdef VISTA_VR
        vr_frame();
#else
        flat_frame();
#endif
    }
#ifdef VISTA_VR
    if (s_xr_ready)
    {
        xr_shutdown();
        s_xr_ready = 0;
    }
#else
    flat_term_window();
    if (s_scene_ready)
    {
        scene_destroy();
        render_destroy();
        assets_destroy_textures(&s_core);
        s_scene_ready = 0;
    }
    if (s_core_ready)
    {
        vkc_core_destroy(&s_core);
        s_core_ready = 0;
    }
#endif
}
