#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "vista.h"

static char g_exedir[MAX_PATH];

static void exedir_init(void)
{
    DWORD n = GetModuleFileNameA(NULL, g_exedir, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        g_exedir[0] = '.';
        g_exedir[1] = 0;
        return;
    }
    for (DWORD i = n; i > 0; i--) {
        if (g_exedir[i - 1] == '\\' || g_exedir[i - 1] == '/') {
            g_exedir[i - 1] = 0;
            return;
        }
    }
    g_exedir[0] = '.';
    g_exedir[1] = 0;
}

uint8_t *plat_load_asset(const char *name, int *out_size)
{
    static const char *prefixes[4] = { "", "../", "assets/", "../assets/" };
    for (int i = 0; i < 4; i++) {
        char path[MAX_PATH * 2];
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
    fflush(stdout);
}

double plat_time(void)
{
    static LARGE_INTEGER freq;
    static LARGE_INTEGER start;
    static int inited;
    LARGE_INTEGER now;
    if (!inited) {
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&start);
        inited = 1;
    }
    QueryPerformanceCounter(&now);
    return (double)(now.QuadPart - start.QuadPart) / (double)freq.QuadPart;
}

#ifdef VISTA_VR

int main(void)
{
    exedir_init();
    VkCore core;
    memset(&core, 0, sizeof core);
    if (xr_create(&core) != 0) {
        plat_log("xr_create failed\n");
        return 1;
    }
    game_init(&g_game);
    Input in;
    float dt;
    while (xr_running()) {
        memset(&in, 0, sizeof in);
        dt = 0.0f;
        if (xr_frame(&g_game, &in, &dt) != 0)
            break;
        if (in.quit)
            break;
    }
    xr_shutdown();
    return 0;
}

#else

#define WIN_W 1600
#define WIN_H 900

static float g_mouse_dx, g_mouse_dy;
static bool g_want_quit;

static LRESULT CALLBACK vista_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_INPUT: {
        RAWINPUT ri;
        UINT sz = sizeof ri;
        if (GetRawInputData((HRAWINPUT)lp, RID_INPUT, &ri, &sz, sizeof(RAWINPUTHEADER)) != (UINT)-1 &&
            ri.header.dwType == RIM_TYPEMOUSE) {
            g_mouse_dx += (float)ri.data.mouse.lLastX;
            g_mouse_dy += (float)ri.data.mouse.lLastY;
        }
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE)
            g_want_quit = true;
        return 0;
    case WM_CLOSE:
    case WM_DESTROY:
        g_want_quit = true;
        return 0;
    default:
        return DefWindowProcA(hwnd, msg, wp, lp);
    }
}

static HWND create_window(void)
{
    HINSTANCE hinst = GetModuleHandleA(NULL);
    WNDCLASSA wc;
    memset(&wc, 0, sizeof wc);
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = vista_wndproc;
    wc.hInstance = hinst;
    wc.hCursor = LoadCursorA(NULL, (LPCSTR)IDC_ARROW);
    wc.lpszClassName = "vista_wc";
    if (!RegisterClassA(&wc))
        return NULL;
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT r = { 0, 0, WIN_W, WIN_H };
    AdjustWindowRect(&r, style, FALSE);
    HWND hwnd = CreateWindowA("vista_wc", "vista", style, CW_USEDEFAULT, CW_USEDEFAULT,
                              r.right - r.left, r.bottom - r.top, NULL, NULL, hinst, NULL);
    if (!hwnd)
        return NULL;
    ShowWindow(hwnd, SW_SHOW);
    RAWINPUTDEVICE rid;
    rid.usUsagePage = 0x01;
    rid.usUsage = 0x02;
    rid.dwFlags = 0;
    rid.hwndTarget = hwnd;
    if (!RegisterRawInputDevices(&rid, 1, sizeof rid)) {
        plat_log("RegisterRawInputDevices failed\n");
        return NULL;
    }
    return hwnd;
}

static bool key_down(int vk)
{
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

static void pump_messages(void)
{
    MSG msg;
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT)
            g_want_quit = true;
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
}

int main(void)
{
    exedir_init();
    HWND hwnd = create_window();
    if (!hwnd) {
        plat_log("window creation failed\n");
        return 1;
    }
    VkCore core;
    memset(&core, 0, sizeof core);
    const char *iext[] = { VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME };
    if (vkc_create_instance(&core, iext, 2) != 0) {
        plat_log("vkc_create_instance failed\n");
        return 1;
    }
    if (vkc_pick_device(&core, VK_NULL_HANDLE) != 0) {
        plat_log("vkc_pick_device failed\n");
        return 1;
    }
    const char *dext[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    if (vkc_create_device(&core, dext, 1) != 0) {
        plat_log("vkc_create_device failed\n");
        return 1;
    }
    if (vkc_finish_setup(&core) != 0) {
        plat_log("vkc_finish_setup failed\n");
        return 1;
    }
    VkWin32SurfaceCreateInfoKHR sci;
    memset(&sci, 0, sizeof sci);
    sci.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    sci.hinstance = GetModuleHandleA(NULL);
    sci.hwnd = hwnd;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkResult vr = vkCreateWin32SurfaceKHR(core.instance, &sci, NULL, &surface);
    if (vr != VK_SUCCESS) {
        plat_log("vkCreateWin32SurfaceKHR failed: %d\n", (int)vr);
        return 1;
    }
    FlatChain fc;
    memset(&fc, 0, sizeof fc);
    if (vkc_flatchain_create(&fc, &core, surface, WIN_W, WIN_H) != 0) {
        plat_log("vkc_flatchain_create failed\n");
        return 1;
    }
    if (render_init(&core, fc.rt.width, fc.rt.height, 1) != 0) {
        plat_log("render_init failed\n");
        return 1;
    }
    if (assets_load_textures(&core) != 0) {
        plat_log("assets_load_textures failed\n");
        return 1;
    }
    if (scene_init(&core, render_scene_rp(), false) != 0) {
        plat_log("scene_init failed\n");
        return 1;
    }
    if (render_post_init(&core, fc.rt.rp) != 0) {
        plat_log("render_post_init failed\n");
        return 1;
    }
    game_init(&g_game);
    const char *smoke_env = getenv("VISTA_SMOKE");
    bool smoke = smoke_env && strcmp(smoke_env, "1") == 0;
    const char *cam_env = getenv("VISTA_CAM");
    if (cam_env)
    {
        float cx, cz, cyaw, cpitch, ctime;
        if (sscanf(cam_env, "%f,%f,%f,%f,%f", &cx, &cz, &cyaw, &cpitch, &ctime) == 5)
        {
            g_game.pos.x = cx;
            g_game.pos.z = cz;
            g_game.pos.y = terrain_height_at(cx, cz) + EYE_HEIGHT;
            g_game.yaw = cyaw;
            g_game.pitch = cpitch;
            g_game.time = ctime;
        }
    }
    int smoke_frames = 0;
    int rc = 0;
    double prev = plat_time();
    while (!g_want_quit) {
        pump_messages();
        if (g_want_quit)
            break;
        Input in;
        memset(&in, 0, sizeof in);
        in.move_x = (key_down('D') ? 1.0f : 0.0f) - (key_down('A') ? 1.0f : 0.0f);
        in.move_y = (key_down('W') ? 1.0f : 0.0f) - (key_down('S') ? 1.0f : 0.0f);
        in.look_dx = g_mouse_dx;
        in.look_dy = g_mouse_dy;
        g_mouse_dx = 0.0f;
        g_mouse_dy = 0.0f;
        in.sprint = key_down(VK_SHIFT);
        in.jump = key_down(VK_SPACE);
        double now = plat_time();
        float dt = (float)(now - prev);
        prev = now;
        if (dt < 0.0f)
            dt = 0.0f;
        if (dt > 0.1f)
            dt = 0.1f;
        game_update(&g_game, &in, dt);
        FrameUBO ubo;
        game_flat_ubo(&g_game, (float)WIN_W / (float)WIN_H, &ubo);
        if (smoke && ++smoke_frames >= 120) {
            char sp[MAX_PATH * 2];
            snprintf(sp, sizeof sp, "%s/smoke.png", g_exedir);
            if (vkc_flatchain_screenshot(&fc, &ubo, sp) != 0) {
                plat_log("screenshot failed\n");
                rc = 1;
            } else {
                plat_log("SMOKE OK\n");
            }
            break;
        }
        if (vkc_flatchain_frame(&fc, &ubo) != 0) {
            plat_log("vkc_flatchain_frame failed\n");
            rc = 1;
            break;
        }
    }
    scene_destroy();
    render_destroy();
    assets_destroy_textures(&core);
    vkc_flatchain_destroy(&fc);
    vkDestroySurfaceKHR(core.instance, surface, NULL);
    vkc_core_destroy(&core);
    return rc;
}

#endif
