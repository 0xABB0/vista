#include "vista.h"

#ifdef VISTA_VR

#include <stdlib.h>
#include <string.h>

#define XR_NO_PROTOTYPES
#define XR_USE_GRAPHICS_API_VULKAN

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef __ANDROID__
#define XR_USE_PLATFORM_ANDROID
#include <jni.h>
#include <dlfcn.h>
#include <time.h>
#include "android_native_app_glue.h"
extern struct android_app *gapp;
#endif

#include "openxr/openxr.h"
#include "openxr/openxr_platform.h"

#define XR_FN_LIST(X) \
    X(xrDestroyInstance) \
    X(xrPollEvent) \
    X(xrGetSystem) \
    X(xrCreateSession) \
    X(xrDestroySession) \
    X(xrBeginSession) \
    X(xrEndSession) \
    X(xrRequestExitSession) \
    X(xrEnumerateReferenceSpaces) \
    X(xrCreateReferenceSpace) \
    X(xrDestroySpace) \
    X(xrEnumerateViewConfigurationViews) \
    X(xrEnumerateSwapchainFormats) \
    X(xrCreateSwapchain) \
    X(xrDestroySwapchain) \
    X(xrEnumerateSwapchainImages) \
    X(xrAcquireSwapchainImage) \
    X(xrWaitSwapchainImage) \
    X(xrReleaseSwapchainImage) \
    X(xrWaitFrame) \
    X(xrBeginFrame) \
    X(xrEndFrame) \
    X(xrLocateViews) \
    X(xrStringToPath) \
    X(xrCreateActionSet) \
    X(xrDestroyActionSet) \
    X(xrCreateAction) \
    X(xrDestroyAction) \
    X(xrSuggestInteractionProfileBindings) \
    X(xrAttachSessionActionSets) \
    X(xrSyncActions) \
    X(xrGetActionStateVector2f) \
    X(xrGetActionStateBoolean) \
    X(xrGetVulkanGraphicsRequirements2KHR) \
    X(xrCreateVulkanInstanceKHR) \
    X(xrGetVulkanGraphicsDevice2KHR) \
    X(xrCreateVulkanDeviceKHR)

static PFN_xrGetInstanceProcAddr xrGetInstanceProcAddr;
static PFN_xrCreateInstance xrCreateInstance;
#define XR_DECL_FN(fn) static PFN_##fn fn;
XR_FN_LIST(XR_DECL_FN)
#undef XR_DECL_FN

#define XRC(call) do{ XrResult xr_res_ = (call); if(XR_FAILED(xr_res_)){ plat_log("xr: %s -> %d", #call, (int)xr_res_); return -1; } }while(0)
#define XRW(call) do{ XrResult xr_res_ = (call); if(XR_FAILED(xr_res_)) plat_log("xr: %s -> %d", #call, (int)xr_res_); }while(0)
#define VKX(call) do{ VkResult vk_res_ = (call); if(vk_res_ < 0){ plat_log("xr: %s -> %d", #call, (int)vk_res_); return -1; } }while(0)

#define XR_SNAP_TURN_RAD 0.52359878f
#define XR_ZNEAR 0.05f
#define XR_ZFAR 6000.0f

struct XrCtx {
    void *lib;
    VkCore *core;
    XrInstance instance;
    XrSystemId system;
    XrSession session;
    XrSpace space;
    XrSwapchain swapchain;
    uint32_t width, height;
    VkFormat format;
    XrViewConfigurationView vcfg[2];
    XrView views[2];
    RenderTargets rt;
    VkCommandBuffer cmd[VISTA_FRAMES];
    VkFence fence[VISTA_FRAMES];
    uint32_t slot;
    XrSessionState state;
    bool session_running;
    bool exit_requested;
    bool initialized;
    bool turn_latched;
    XrTime last_time;
    XrActionSet actionset;
    XrAction act_move, act_turn, act_jump, act_quit;
};

static struct XrCtx g_xr;

static int xr_load_library(struct XrCtx *x)
{
#ifdef _WIN32
    HMODULE lib = LoadLibraryA("openxr_loader.dll");
    if(!lib){ plat_log("xr: openxr_loader.dll not found"); return -1; }
    x->lib = (void*)lib;
    xrGetInstanceProcAddr = (PFN_xrGetInstanceProcAddr)GetProcAddress(lib, "xrGetInstanceProcAddr");
#else
    void *lib = dlopen("libopenxr_loader.so", RTLD_NOW | RTLD_LOCAL);
    if(!lib){ plat_log("xr: libopenxr_loader.so not found"); return -1; }
    x->lib = lib;
    xrGetInstanceProcAddr = (PFN_xrGetInstanceProcAddr)dlsym(lib, "xrGetInstanceProcAddr");
#endif
    if(!xrGetInstanceProcAddr){ plat_log("xr: xrGetInstanceProcAddr not found"); return -1; }
    return 0;
}

static int xr_load_instance_fns(struct XrCtx *x)
{
#define XR_LOAD_FN(fn) XRC(xrGetInstanceProcAddr(x->instance, #fn, (PFN_xrVoidFunction*)&fn));
    XR_FN_LIST(XR_LOAD_FN)
#undef XR_LOAD_FN
    return 0;
}

static int xr_init_instance(struct XrCtx *x)
{
#ifdef __ANDROID__
    PFN_xrInitializeLoaderKHR init_loader = NULL;
    XRC(xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR", (PFN_xrVoidFunction*)&init_loader));
    XrLoaderInitInfoAndroidKHR li = { XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR };
    li.applicationVM = gapp->activity->vm;
    li.applicationContext = gapp->activity->clazz;
    XRC(init_loader((const XrLoaderInitInfoBaseHeaderKHR*)&li));
#endif
    XRC(xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrCreateInstance", (PFN_xrVoidFunction*)&xrCreateInstance));
    const char *exts[2];
    uint32_t nexts = 0;
    exts[nexts++] = XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME;
    XrInstanceCreateInfo ci = { XR_TYPE_INSTANCE_CREATE_INFO };
#ifdef __ANDROID__
    exts[nexts++] = XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME;
    XrInstanceCreateInfoAndroidKHR ai = { XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR };
    ai.applicationVM = gapp->activity->vm;
    ai.applicationActivity = gapp->activity->clazz;
    ci.next = &ai;
#endif
    strncpy(ci.applicationInfo.applicationName, "vista", XR_MAX_APPLICATION_NAME_SIZE - 1);
    ci.applicationInfo.applicationVersion = 1;
    strncpy(ci.applicationInfo.engineName, "vista", XR_MAX_ENGINE_NAME_SIZE - 1);
    ci.applicationInfo.engineVersion = 1;
    ci.applicationInfo.apiVersion = XR_API_VERSION_1_0;
    ci.enabledExtensionCount = nexts;
    ci.enabledExtensionNames = exts;
    XRC(xrCreateInstance(&ci, &x->instance));
    if(xr_load_instance_fns(x)) return -1;
    XrSystemGetInfo sgi = { XR_TYPE_SYSTEM_GET_INFO };
    sgi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XRC(xrGetSystem(x->instance, &sgi, &x->system));
    return 0;
}

static int xr_init_vulkan(struct XrCtx *x, VkCore *c)
{
    VKX(volkInitialize());
    XrGraphicsRequirementsVulkan2KHR reqs = { XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR };
    XRC(xrGetVulkanGraphicsRequirements2KHR(x->instance, x->system, &reqs));
    if(reqs.minApiVersionSupported > XR_MAKE_VERSION(1, 1, 0)){
        plat_log("xr: runtime requires Vulkan %u.%u+, app targets 1.1",
                 (uint32_t)XR_VERSION_MAJOR(reqs.minApiVersionSupported),
                 (uint32_t)XR_VERSION_MINOR(reqs.minApiVersionSupported));
        return -1;
    }
    VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    app.pApplicationName = "vista";
    app.applicationVersion = 1;
    app.pEngineName = "vista";
    app.engineVersion = 1;
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    ici.pApplicationInfo = &app;
    XrVulkanInstanceCreateInfoKHR xici = { XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR };
    xici.systemId = x->system;
    xici.pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
    xici.vulkanCreateInfo = &ici;
    VkInstance vinst = VK_NULL_HANDLE;
    VkResult vres = VK_SUCCESS;
    XRC(xrCreateVulkanInstanceKHR(x->instance, &xici, &vinst, &vres));
    VKX(vres);
    c->instance = vinst;
    volkLoadInstance(vinst);
    XrVulkanGraphicsDeviceGetInfoKHR gdi = { XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR };
    gdi.systemId = x->system;
    gdi.vulkanInstance = vinst;
    VkPhysicalDevice phys = VK_NULL_HANDLE;
    XRC(xrGetVulkanGraphicsDevice2KHR(x->instance, &gdi, &phys));
    if(vkc_pick_device(c, phys)){ plat_log("xr: vkc_pick_device failed"); return -1; }
    VkPhysicalDeviceMultiviewFeatures mv_have = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES };
    VkPhysicalDeviceFeatures2 f2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    f2.pNext = &mv_have;
    vkGetPhysicalDeviceFeatures2(c->phys, &f2);
    if(!mv_have.multiview){ plat_log("xr: multiview not supported"); return -1; }
    VkPhysicalDeviceFeatures enabled = {0};
    enabled.tessellationShader = (f2.features.tessellationShader && mv_have.multiviewTessellationShader) ? VK_TRUE : VK_FALSE;
    enabled.samplerAnisotropy = f2.features.samplerAnisotropy;
    VkPhysicalDeviceMultiviewFeatures mv_on = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES };
    mv_on.multiview = VK_TRUE;
    mv_on.multiviewTessellationShader = enabled.tessellationShader;
    c->has_tess = enabled.tessellationShader != 0;
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    qci.queueFamilyIndex = c->qfam;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;
    VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    dci.pNext = &mv_on;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.pEnabledFeatures = &enabled;
    XrVulkanDeviceCreateInfoKHR xdci = { XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR };
    xdci.systemId = x->system;
    xdci.pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
    xdci.vulkanPhysicalDevice = c->phys;
    xdci.vulkanCreateInfo = &dci;
    VkDevice dev = VK_NULL_HANDLE;
    XRC(xrCreateVulkanDeviceKHR(x->instance, &xdci, &dev, &vres));
    VKX(vres);
    c->device = dev;
    volkLoadDevice(dev);
    vkGetDeviceQueue(c->device, c->qfam, 0, &c->queue);
    if(vkc_finish_setup(c)){ plat_log("xr: vkc_finish_setup failed"); return -1; }
    return 0;
}

static int xr_init_session(struct XrCtx *x, VkCore *c)
{
    XrGraphicsBindingVulkan2KHR gb = { XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR };
    gb.instance = c->instance;
    gb.physicalDevice = c->phys;
    gb.device = c->device;
    gb.queueFamilyIndex = c->qfam;
    gb.queueIndex = 0;
    XrSessionCreateInfo sci = { XR_TYPE_SESSION_CREATE_INFO };
    sci.next = &gb;
    sci.systemId = x->system;
    XRC(xrCreateSession(x->instance, &sci, &x->session));
    uint32_t nspaces = 0;
    XrReferenceSpaceType types[16];
    XRC(xrEnumerateReferenceSpaces(x->session, 0, &nspaces, NULL));
    if(nspaces > 16) nspaces = 16;
    XRC(xrEnumerateReferenceSpaces(x->session, nspaces, &nspaces, types));
    XrReferenceSpaceType chosen = XR_REFERENCE_SPACE_TYPE_LOCAL;
    for(uint32_t i = 0; i < nspaces; i++)
        if(types[i] == XR_REFERENCE_SPACE_TYPE_STAGE) chosen = XR_REFERENCE_SPACE_TYPE_STAGE;
    XrReferenceSpaceCreateInfo rci = { XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
    rci.referenceSpaceType = chosen;
    rci.poseInReferenceSpace.orientation.w = 1.0f;
    XRC(xrCreateReferenceSpace(x->session, &rci, &x->space));
    return 0;
}

static int xr_init_swapchain(struct XrCtx *x, VkCore *c)
{
    uint32_t nviews = 0;
    x->vcfg[0].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
    x->vcfg[1].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
    XRC(xrEnumerateViewConfigurationViews(x->instance, x->system,
        XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 2, &nviews, x->vcfg));
    if(nviews != 2){ plat_log("xr: expected 2 views, got %u", nviews); return -1; }
    x->width = x->vcfg[0].recommendedImageRectWidth;
    x->height = x->vcfg[0].recommendedImageRectHeight;
#ifdef __ANDROID__
    x->width = x->width * 3 / 4;
    x->height = x->height * 3 / 4;
#endif
    uint32_t nfmt = 0;
    XRC(xrEnumerateSwapchainFormats(x->session, 0, &nfmt, NULL));
    if(nfmt == 0){ plat_log("xr: no swapchain formats"); return -1; }
    int64_t *fmts = malloc(nfmt * sizeof *fmts);
    if(!fmts){ plat_log("xr: format alloc failed"); return -1; }
    XrResult fres = xrEnumerateSwapchainFormats(x->session, nfmt, &nfmt, fmts);
    if(XR_FAILED(fres)){ plat_log("xr: xrEnumerateSwapchainFormats -> %d", (int)fres); free(fmts); return -1; }
    int64_t pick = fmts[0];
    for(uint32_t i = 0; i < nfmt; i++){
        if(fmts[i] == VK_FORMAT_R8G8B8A8_SRGB || fmts[i] == VK_FORMAT_B8G8R8A8_SRGB){ pick = fmts[i]; break; }
    }
    free(fmts);
    x->format = (VkFormat)pick;
    XrSwapchainCreateInfo sc = { XR_TYPE_SWAPCHAIN_CREATE_INFO };
    sc.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    sc.format = pick;
    sc.sampleCount = 1;
    sc.width = x->width;
    sc.height = x->height;
    sc.faceCount = 1;
    sc.arraySize = 2;
    sc.mipCount = 1;
    XRC(xrCreateSwapchain(x->session, &sc, &x->swapchain));
    uint32_t nimg = 0;
    XRC(xrEnumerateSwapchainImages(x->swapchain, 0, &nimg, NULL));
    if(nimg == 0 || nimg > 8){ plat_log("xr: bad swapchain image count %u", nimg); return -1; }
    XrSwapchainImageVulkan2KHR imgs[8];
    memset(imgs, 0, sizeof imgs);
    for(uint32_t i = 0; i < nimg; i++) imgs[i].type = XR_TYPE_SWAPCHAIN_IMAGE_VULKAN2_KHR;
    XRC(xrEnumerateSwapchainImages(x->swapchain, nimg, &nimg, (XrSwapchainImageBaseHeader*)imgs));
    VkImage vkimgs[8];
    for(uint32_t i = 0; i < nimg; i++) vkimgs[i] = imgs[i].image;
    if(vkc_targets_create(&x->rt, c, x->width, x->height, 2, x->format, vkimgs, nimg,
                          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)){
        plat_log("xr: vkc_targets_create failed");
        return -1;
    }
    return 0;
}

static XrPath xr_path(struct XrCtx *x, const char *s)
{
    XrPath p = XR_NULL_PATH;
    XRW(xrStringToPath(x->instance, s, &p));
    return p;
}

static int xr_make_action(struct XrCtx *x, XrActionType t, const char *name, const char *loc, XrAction *out)
{
    XrActionCreateInfo ci = { XR_TYPE_ACTION_CREATE_INFO };
    ci.actionType = t;
    strncpy(ci.actionName, name, XR_MAX_ACTION_NAME_SIZE - 1);
    strncpy(ci.localizedActionName, loc, XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
    XRC(xrCreateAction(x->actionset, &ci, out));
    return 0;
}

static int xr_init_actions(struct XrCtx *x)
{
    XrActionSetCreateInfo asci = { XR_TYPE_ACTION_SET_CREATE_INFO };
    strncpy(asci.actionSetName, "gameplay", XR_MAX_ACTION_SET_NAME_SIZE - 1);
    strncpy(asci.localizedActionSetName, "Gameplay", XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE - 1);
    XRC(xrCreateActionSet(x->instance, &asci, &x->actionset));
    if(xr_make_action(x, XR_ACTION_TYPE_VECTOR2F_INPUT, "move", "Move", &x->act_move)) return -1;
    if(xr_make_action(x, XR_ACTION_TYPE_VECTOR2F_INPUT, "turn", "Turn", &x->act_turn)) return -1;
    if(xr_make_action(x, XR_ACTION_TYPE_BOOLEAN_INPUT, "jump", "Jump", &x->act_jump)) return -1;
    if(xr_make_action(x, XR_ACTION_TYPE_BOOLEAN_INPUT, "quit", "Quit", &x->act_quit)) return -1;
    XrActionSuggestedBinding simple[2] = {
        { x->act_jump, xr_path(x, "/user/hand/right/input/select/click") },
        { x->act_quit, xr_path(x, "/user/hand/left/input/menu/click") },
    };
    XrInteractionProfileSuggestedBinding sp = { XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
    sp.interactionProfile = xr_path(x, "/interaction_profiles/khr/simple_controller");
    sp.suggestedBindings = simple;
    sp.countSuggestedBindings = 2;
    XRC(xrSuggestInteractionProfileBindings(x->instance, &sp));
    XrActionSuggestedBinding touch[5] = {
        { x->act_move, xr_path(x, "/user/hand/left/input/thumbstick") },
        { x->act_turn, xr_path(x, "/user/hand/right/input/thumbstick") },
        { x->act_jump, xr_path(x, "/user/hand/right/input/a/click") },
        { x->act_jump, xr_path(x, "/user/hand/right/input/trigger/value") },
        { x->act_quit, xr_path(x, "/user/hand/left/input/menu/click") },
    };
    XrInteractionProfileSuggestedBinding tp = { XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
    tp.interactionProfile = xr_path(x, "/interaction_profiles/oculus/touch_controller");
    tp.suggestedBindings = touch;
    tp.countSuggestedBindings = 5;
    XRC(xrSuggestInteractionProfileBindings(x->instance, &tp));
    XrSessionActionSetsAttachInfo ai = { XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO };
    ai.countActionSets = 1;
    ai.actionSets = &x->actionset;
    XRC(xrAttachSessionActionSets(x->session, &ai));
    return 0;
}

static int xr_init_frames(struct XrCtx *x, VkCore *c)
{
    VkCommandBufferAllocateInfo cai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    cai.commandPool = c->cmdpool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = VISTA_FRAMES;
    VKX(vkAllocateCommandBuffers(c->device, &cai, x->cmd));
    VkFenceCreateInfo fci = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for(uint32_t i = 0; i < VISTA_FRAMES; i++)
        VKX(vkCreateFence(c->device, &fci, NULL, &x->fence[i]));
    return 0;
}

int xr_create(VkCore *c)
{
    struct XrCtx *x = &g_xr;
    memset(x, 0, sizeof *x);
    x->core = c;
    if(xr_load_library(x)) return -1;
    if(xr_init_instance(x)) return -1;
    if(xr_init_vulkan(x, c)) return -1;
    if(xr_init_session(x, c)) return -1;
    if(xr_init_swapchain(x, c)) return -1;
    if(render_init(c, x->width, x->height, 2)){ plat_log("xr: render_init failed"); return -1; }
    if(assets_load_textures(c)){ plat_log("xr: assets_load_textures failed"); return -1; }
    if(scene_init(c, render_scene_rp(), true)){ plat_log("xr: scene_init failed"); return -1; }
    if(render_post_init(c, x->rt.rp)){ plat_log("xr: render_post_init failed"); return -1; }
    if(xr_init_actions(x)) return -1;
    if(xr_init_frames(x, c)) return -1;
    x->initialized = true;
    return 0;
}

int xr_running(void)
{
    return g_xr.initialized && !g_xr.exit_requested;
}

static int xr_poll_events(struct XrCtx *x)
{
    XrEventDataBuffer ev;
    for(;;){
        ev.type = XR_TYPE_EVENT_DATA_BUFFER;
        ev.next = NULL;
        XrResult r = xrPollEvent(x->instance, &ev);
        if(r == XR_EVENT_UNAVAILABLE) break;
        if(XR_FAILED(r)){ plat_log("xr: xrPollEvent -> %d", (int)r); return -1; }
        switch(ev.type){
        case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
            x->session_running = false;
            x->exit_requested = true;
            break;
        case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
            const XrEventDataSessionStateChanged *sc = (const XrEventDataSessionStateChanged*)&ev;
            x->state = sc->state;
            switch(sc->state){
            case XR_SESSION_STATE_READY: {
                XrSessionBeginInfo bi = { XR_TYPE_SESSION_BEGIN_INFO };
                bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                XRC(xrBeginSession(x->session, &bi));
                x->session_running = true;
                x->last_time = 0;
                break; }
            case XR_SESSION_STATE_STOPPING:
                XRW(xrEndSession(x->session));
                x->session_running = false;
                break;
            case XR_SESSION_STATE_EXITING:
            case XR_SESSION_STATE_LOSS_PENDING:
                x->session_running = false;
                x->exit_requested = true;
                break;
            default:
                break;
            }
            break; }
        default:
            break;
        }
    }
    return 0;
}

static int xr_sync_input(struct XrCtx *x, Input *in)
{
    XrActiveActionSet aas = { x->actionset, XR_NULL_PATH };
    XrActionsSyncInfo si = { XR_TYPE_ACTIONS_SYNC_INFO };
    si.countActiveActionSets = 1;
    si.activeActionSets = &aas;
    XrResult r = xrSyncActions(x->session, &si);
    if(XR_FAILED(r)){ plat_log("xr: xrSyncActions -> %d", (int)r); return -1; }
    if(r == XR_SESSION_NOT_FOCUSED) return 0;
    XrActionStateGetInfo gi = { XR_TYPE_ACTION_STATE_GET_INFO };
    XrActionStateVector2f mv = { XR_TYPE_ACTION_STATE_VECTOR2F };
    gi.action = x->act_move;
    XRC(xrGetActionStateVector2f(x->session, &gi, &mv));
    if(mv.isActive){
        in->move_x = mv.currentState.x;
        in->move_y = mv.currentState.y;
    }
    XrActionStateVector2f tv = { XR_TYPE_ACTION_STATE_VECTOR2F };
    gi.action = x->act_turn;
    XRC(xrGetActionStateVector2f(x->session, &gi, &tv));
    float tx = tv.isActive ? tv.currentState.x : 0.0f;
    if(!x->turn_latched){
        if(tx > 0.6f){ in->snap_turn = -XR_SNAP_TURN_RAD; x->turn_latched = true; }
        else if(tx < -0.6f){ in->snap_turn = XR_SNAP_TURN_RAD; x->turn_latched = true; }
    } else if(fabsf(tx) < 0.4f){
        x->turn_latched = false;
    }
    XrActionStateBoolean bs = { XR_TYPE_ACTION_STATE_BOOLEAN };
    gi.action = x->act_jump;
    XRC(xrGetActionStateBoolean(x->session, &gi, &bs));
    in->jump = bs.isActive && bs.currentState && bs.changedSinceLastSync;
    XrActionStateBoolean qs = { XR_TYPE_ACTION_STATE_BOOLEAN };
    gi.action = x->act_quit;
    XRC(xrGetActionStateBoolean(x->session, &gi, &qs));
    if(qs.isActive && qs.currentState && qs.changedSinceLastSync){
        in->quit = true;
        XRW(xrRequestExitSession(x->session));
    }
    return 0;
}

static void xr_idle_sleep(void)
{
#ifdef _WIN32
    Sleep(10);
#else
    struct timespec ts = { 0, 10000000 };
    nanosleep(&ts, NULL);
#endif
}

static int xr_render(struct XrCtx *x, const GameState *g, const XrFrameState *fs,
                     XrCompositionLayerProjectionView *pviews, bool *rendered)
{
    VkCore *c = x->core;
    XrViewLocateInfo vli = { XR_TYPE_VIEW_LOCATE_INFO };
    vli.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    vli.displayTime = fs->predictedDisplayTime;
    vli.space = x->space;
    XrViewState vs = { XR_TYPE_VIEW_STATE };
    x->views[0].type = XR_TYPE_VIEW;
    x->views[0].next = NULL;
    x->views[1].type = XR_TYPE_VIEW;
    x->views[1].next = NULL;
    uint32_t nv = 0;
    XRC(xrLocateViews(x->session, &vli, &vs, 2, &nv, x->views));
    if(nv != 2) return 0;
    if(!(vs.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT)) return 0;
    if(!(vs.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT)) return 0;
    q4 qyaw = { 0.0f, sinf(g->yaw * 0.5f), 0.0f, cosf(g->yaw * 0.5f) };
    m4 body = m4from_rt(qyaw, g->pos);
    FrameUBO ubo;
    memset(&ubo, 0, sizeof ubo);
    for(int i = 0; i < 2; i++){
        XrPosef p = x->views[i].pose;
        m4 eye = m4from_rt((q4){ p.orientation.x, p.orientation.y, p.orientation.z, p.orientation.w },
                           (v3){ p.position.x, p.position.y, p.position.z });
        m4 view = m4inverse_rt(m4mul(body, eye));
        m4 proj = m4frustum_xr(x->views[i].fov.angleLeft, x->views[i].fov.angleRight,
                               x->views[i].fov.angleUp, x->views[i].fov.angleDown,
                               XR_ZNEAR, XR_ZFAR);
        ubo.viewproj[i] = m4mul(proj, view);
    }
    v3 head = {
        (x->views[0].pose.position.x + x->views[1].pose.position.x) * 0.5f,
        (x->views[0].pose.position.y + x->views[1].pose.position.y) * 0.5f,
        (x->views[0].pose.position.z + x->views[1].pose.position.z) * 0.5f,
    };
    v3 campos = v3add(g->pos, q4rotate(qyaw, head));
    ubo.campos[0] = campos.x;
    ubo.campos[1] = campos.y;
    ubo.campos[2] = campos.z;
    ubo.campos[3] = 0.0f;
    v3 sd = game_sun_dir(g);
    ubo.sundir[0] = sd.x;
    ubo.sundir[1] = sd.y;
    ubo.sundir[2] = sd.z;
    ubo.sundir[3] = 0.0f;
    game_fill_light(g, &ubo);
#ifdef __ANDROID__
    ubo.timeparams[2] = 0.5f;
    ubo.timeparams[3] = 0.5f;
#endif
    ubo.post[1] = 0.0f;
    uint32_t img = 0;
    XrSwapchainImageAcquireInfo aci = { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
    XRC(xrAcquireSwapchainImage(x->swapchain, &aci, &img));
    XrSwapchainImageWaitInfo wi = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
    wi.timeout = XR_INFINITE_DURATION;
    XRC(xrWaitSwapchainImage(x->swapchain, &wi));
    uint32_t slot = x->slot;
    VKX(vkWaitForFences(c->device, 1, &x->fence[slot], VK_TRUE, UINT64_MAX));
    VKX(vkResetFences(c->device, 1, &x->fence[slot]));
    veg_update(campos, slot);
    scene_update_ubo(slot, &ubo);
    VkCommandBufferBeginInfo bi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VKX(vkBeginCommandBuffer(x->cmd[slot], &bi));
    render_record(x->cmd[slot], slot, x->rt.fbs[img], x->width, x->height);
    VKX(vkEndCommandBuffer(x->cmd[slot]));
    VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    si.commandBufferCount = 1;
    si.pCommandBuffers = &x->cmd[slot];
    VKX(vkQueueSubmit(c->queue, 1, &si, x->fence[slot]));
    XrSwapchainImageReleaseInfo ri = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
    XRC(xrReleaseSwapchainImage(x->swapchain, &ri));
    x->slot = (slot + 1) % VISTA_FRAMES;
    for(uint32_t i = 0; i < 2; i++){
        pviews[i].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
        pviews[i].next = NULL;
        pviews[i].pose = x->views[i].pose;
        pviews[i].fov = x->views[i].fov;
        pviews[i].subImage.swapchain = x->swapchain;
        pviews[i].subImage.imageRect.offset.x = 0;
        pviews[i].subImage.imageRect.offset.y = 0;
        pviews[i].subImage.imageRect.extent.width = (int32_t)x->width;
        pviews[i].subImage.imageRect.extent.height = (int32_t)x->height;
        pviews[i].subImage.imageArrayIndex = i;
    }
    *rendered = true;
    return 0;
}

int xr_frame(GameState *g, Input *in, float *out_dt)
{
    struct XrCtx *x = &g_xr;
    memset(in, 0, sizeof *in);
    *out_dt = 0.0f;
    if(!x->initialized) return -1;
    if(xr_poll_events(x)) return -1;
    if(x->exit_requested) return 0;
    if(!x->session_running){
        xr_idle_sleep();
        return 0;
    }
    XrFrameState fs = { XR_TYPE_FRAME_STATE };
    XrFrameWaitInfo fwi = { XR_TYPE_FRAME_WAIT_INFO };
    XRC(xrWaitFrame(x->session, &fwi, &fs));
    float dt;
    if(x->last_time == 0) dt = 1.0f / 72.0f;
    else dt = (float)((double)(fs.predictedDisplayTime - x->last_time) * 1e-9);
    if(dt < 0.0f) dt = 0.0f;
    if(dt > 0.1f) dt = 0.1f;
    x->last_time = fs.predictedDisplayTime;
    XrFrameBeginInfo fbi = { XR_TYPE_FRAME_BEGIN_INFO };
    XRC(xrBeginFrame(x->session, &fbi));
    if(xr_sync_input(x, in)) return -1;
    game_update(g, in, dt);
    *out_dt = dt;
    bool rendered = false;
    XrCompositionLayerProjectionView pviews[2];
    XrCompositionLayerProjection layer = { XR_TYPE_COMPOSITION_LAYER_PROJECTION };
    if(fs.shouldRender){
        if(xr_render(x, g, &fs, pviews, &rendered)) return -1;
    }
    layer.space = x->space;
    layer.viewCount = 2;
    layer.views = pviews;
    const XrCompositionLayerBaseHeader *layers[1] = { (const XrCompositionLayerBaseHeader*)&layer };
    XrFrameEndInfo fei = { XR_TYPE_FRAME_END_INFO };
    fei.displayTime = fs.predictedDisplayTime;
    fei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    fei.layerCount = rendered ? 1 : 0;
    fei.layers = rendered ? layers : NULL;
    XRC(xrEndFrame(x->session, &fei));
    return 0;
}

void xr_shutdown(void)
{
    struct XrCtx *x = &g_xr;
    VkCore *c = x->core;
    if(c && c->device){
        VkResult r = vkDeviceWaitIdle(c->device);
        if(r < 0) plat_log("xr: vkDeviceWaitIdle -> %d", (int)r);
        scene_destroy();
        render_destroy();
        assets_destroy_textures(c);
        for(uint32_t i = 0; i < VISTA_FRAMES; i++)
            if(x->fence[i]) vkDestroyFence(c->device, x->fence[i], NULL);
        if(x->cmd[0]) vkFreeCommandBuffers(c->device, c->cmdpool, VISTA_FRAMES, x->cmd);
        if(x->rt.rp) vkc_targets_destroy(&x->rt);
    }
    if(x->act_move) XRW(xrDestroyAction(x->act_move));
    if(x->act_turn) XRW(xrDestroyAction(x->act_turn));
    if(x->act_jump) XRW(xrDestroyAction(x->act_jump));
    if(x->act_quit) XRW(xrDestroyAction(x->act_quit));
    if(x->actionset) XRW(xrDestroyActionSet(x->actionset));
    if(x->swapchain) XRW(xrDestroySwapchain(x->swapchain));
    if(x->space) XRW(xrDestroySpace(x->space));
    if(x->session) XRW(xrDestroySession(x->session));
    if(c) vkc_core_destroy(c);
    if(x->instance) XRW(xrDestroyInstance(x->instance));
#ifdef _WIN32
    if(x->lib) FreeLibrary((HMODULE)x->lib);
#else
    if(x->lib) dlclose(x->lib);
#endif
    memset(x, 0, sizeof *x);
}

#endif
