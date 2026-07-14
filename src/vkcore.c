#include "vista.h"
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define VKCHK(x) do { VkResult vkchk_r = (x); if (vkchk_r != VK_SUCCESS) { plat_log("vkcore: %s failed (%d) at %s:%d", #x, (int)vkchk_r, __FILE__, __LINE__); return -1; } } while (0)
#define VKWARN(x) do { VkResult vkwarn_r = (x); if (vkwarn_r != VK_SUCCESS) plat_log("vkcore: %s failed (%d) at %s:%d", #x, (int)vkwarn_r, __FILE__, __LINE__); } while (0)

VkResult (*vkc_instance_hook)(const VkInstanceCreateInfo *ci, VkInstance *out) = 0;
VkResult (*vkc_device_hook)(VkPhysicalDevice phys, const VkDeviceCreateInfo *ci, VkDevice *out) = 0;

static int32_t find_mem(VkCore *c, uint32_t bits, VkMemoryPropertyFlags want)
{
    for (uint32_t i = 0; i < c->memprops.memoryTypeCount; i++)
        if ((bits & (1u << i)) && (c->memprops.memoryTypes[i].propertyFlags & want) == want)
            return (int32_t)i;
    return -1;
}

#ifndef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
#define VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME "VK_KHR_portability_enumeration"
#define VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR 0x00000001
#endif
#ifndef VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME
#define VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME "VK_KHR_portability_subset"
#endif

#define VKC_STYPE_PORTABILITY_SUBSET_FEATURES ((VkStructureType)1000163000)

typedef struct {
    VkStructureType sType;
    void *pNext;
    VkBool32 constantAlphaColorBlendFactors;
    VkBool32 events;
    VkBool32 imageViewFormatReinterpretation;
    VkBool32 imageViewFormatSwizzle;
    VkBool32 imageView2DOn3DImage;
    VkBool32 multisampleArrayImage;
    VkBool32 mutableComparisonSamplers;
    VkBool32 pointPolygons;
    VkBool32 samplerMipLodBias;
    VkBool32 separateStencilMaskRef;
    VkBool32 shaderSampleRateInterpolationFunctions;
    VkBool32 tessellationIsolines;
    VkBool32 tessellationPointMode;
    VkBool32 triangleFans;
    VkBool32 vertexAttributeAccessBeyondStride;
} VkcPortabilityFeatures;

static bool instance_ext_present(const char *name)
{
    uint32_t n = 0;
    bool found = false;
    if (vkEnumerateInstanceExtensionProperties(0, &n, 0) != VK_SUCCESS || !n)
        return false;
    VkExtensionProperties *ep = malloc(n * sizeof *ep);
    if (!ep)
        return false;
    if (vkEnumerateInstanceExtensionProperties(0, &n, ep) == VK_SUCCESS)
        for (uint32_t i = 0; i < n; i++)
            if (!strcmp(ep[i].extensionName, name)) {
                found = true;
                break;
            }
    free(ep);
    return found;
}

static bool device_ext_present(VkPhysicalDevice phys, const char *name)
{
    uint32_t n = 0;
    bool found = false;
    if (vkEnumerateDeviceExtensionProperties(phys, 0, &n, 0) != VK_SUCCESS || !n)
        return false;
    VkExtensionProperties *ep = malloc(n * sizeof *ep);
    if (!ep)
        return false;
    if (vkEnumerateDeviceExtensionProperties(phys, 0, &n, ep) == VK_SUCCESS)
        for (uint32_t i = 0; i < n; i++)
            if (!strcmp(ep[i].extensionName, name)) {
                found = true;
                break;
            }
    free(ep);
    return found;
}

int vkc_create_instance(VkCore *c, const char **ext, uint32_t next)
{
    VKCHK(volkInitialize());
    VkApplicationInfo ai = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "vista",
        .pEngineName = "vista",
        .apiVersion = VK_API_VERSION_1_1,
    };
    const char *layers[1];
    uint32_t nlayers = 0;
    if (getenv("VISTA_SMOKE") || getenv("VISTA_VALIDATE")) {
        uint32_t n = 0;
        if (vkEnumerateInstanceLayerProperties(&n, 0) == VK_SUCCESS && n) {
            VkLayerProperties *lp = malloc(n * sizeof *lp);
            if (lp) {
                if (vkEnumerateInstanceLayerProperties(&n, lp) == VK_SUCCESS)
                    for (uint32_t i = 0; i < n; i++)
                        if (!strcmp(lp[i].layerName, "VK_LAYER_KHRONOS_validation")) {
                            layers[0] = "VK_LAYER_KHRONOS_validation";
                            nlayers = 1;
                            break;
                        }
                free(lp);
            }
        }
    }
    const char *exts[15];
    uint32_t nexts = 0;
    for (uint32_t i = 0; i < next && nexts < 14; i++)
        exts[nexts++] = ext[i];
    VkInstanceCreateFlags flags = 0;
    if (instance_ext_present(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
        exts[nexts++] = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
        flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }
    VkInstanceCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .flags = flags,
        .pApplicationInfo = &ai,
        .enabledLayerCount = nlayers,
        .ppEnabledLayerNames = layers,
        .enabledExtensionCount = nexts,
        .ppEnabledExtensionNames = exts,
    };
    if (vkc_instance_hook)
        VKCHK(vkc_instance_hook(&ci, &c->instance));
    else
        VKCHK(vkCreateInstance(&ci, 0, &c->instance));
    volkLoadInstance(c->instance);
    return 0;
}

int vkc_pick_device(VkCore *c, VkPhysicalDevice forced)
{
    if (forced) {
        c->phys = forced;
    } else {
        uint32_t n = 0;
        VKCHK(vkEnumeratePhysicalDevices(c->instance, &n, 0));
        if (!n) return -1;
        if (n > 16) n = 16;
        VkPhysicalDevice devs[16];
        VkResult r = vkEnumeratePhysicalDevices(c->instance, &n, devs);
        if (r != VK_SUCCESS && r != VK_INCOMPLETE) return -1;
        c->phys = devs[0];
        for (uint32_t i = 0; i < n; i++) {
            VkPhysicalDeviceProperties p;
            vkGetPhysicalDeviceProperties(devs[i], &p);
            if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                c->phys = devs[i];
                break;
            }
        }
    }
    vkGetPhysicalDeviceProperties(c->phys, &c->props);
    vkGetPhysicalDeviceMemoryProperties(c->phys, &c->memprops);
    VkPhysicalDeviceFeatures f;
    vkGetPhysicalDeviceFeatures(c->phys, &f);
    c->has_tess = f.tessellationShader != 0;
    const char *notess = getenv("VISTA_NO_TESS");
    if (notess && !strcmp(notess, "1"))
        c->has_tess = false;
#ifdef __ANDROID__
    c->tier = 0;
#else
    c->tier = 1;
#endif
    const char *tier_env = getenv("VISTA_TIER");
    if (tier_env && (tier_env[0] == '0' || tier_env[0] == '1'))
        c->tier = (uint32_t)(tier_env[0] - '0');
    uint32_t qn = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(c->phys, &qn, 0);
    if (qn > 32) qn = 32;
    VkQueueFamilyProperties qs[32];
    vkGetPhysicalDeviceQueueFamilyProperties(c->phys, &qn, qs);
    c->qfam = UINT32_MAX;
    for (uint32_t i = 0; i < qn; i++) {
        if (!(qs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) continue;
        if (c->qfam == UINT32_MAX) c->qfam = i;
#ifdef VK_USE_PLATFORM_WIN32_KHR
        if (vkGetPhysicalDeviceWin32PresentationSupportKHR &&
            vkGetPhysicalDeviceWin32PresentationSupportKHR(c->phys, i)) {
            c->qfam = i;
            break;
        }
#else
        break;
#endif
    }
    if (c->qfam == UINT32_MAX) return -1;
    VkFormatProperties fp;
    vkGetPhysicalDeviceFormatProperties(c->phys, VK_FORMAT_D24_UNORM_S8_UINT, &fp);
    if (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
        c->depth_format = VK_FORMAT_D24_UNORM_S8_UINT;
    else
        c->depth_format = VK_FORMAT_D32_SFLOAT;
    VkSampleCountFlags counts = c->props.limits.framebufferColorSampleCounts & c->props.limits.framebufferDepthSampleCounts;
#ifdef __ANDROID__
    c->msaa = (counts & VK_SAMPLE_COUNT_2_BIT) ? VK_SAMPLE_COUNT_2_BIT : VK_SAMPLE_COUNT_1_BIT;
#else
    c->msaa = (counts & VK_SAMPLE_COUNT_4_BIT) ? VK_SAMPLE_COUNT_4_BIT :
              (counts & VK_SAMPLE_COUNT_2_BIT) ? VK_SAMPLE_COUNT_2_BIT : VK_SAMPLE_COUNT_1_BIT;
#endif
    return 0;
}

int vkc_create_device(VkCore *c, const char **ext, uint32_t next)
{
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = c->qfam,
        .queueCount = 1,
        .pQueuePriorities = &prio,
    };
    VkPhysicalDeviceFeatures sup;
    vkGetPhysicalDeviceFeatures(c->phys, &sup);
    VkPhysicalDeviceFeatures en = {0};
    en.tessellationShader = sup.tessellationShader;
    en.samplerAnisotropy = sup.samplerAnisotropy;
    en.multiDrawIndirect = sup.multiDrawIndirect;
    VkPhysicalDeviceMultiviewFeatures mv = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES };
    VkPhysicalDeviceFeatures2 f2 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &mv };
    vkGetPhysicalDeviceFeatures2(c->phys, &f2);
    mv.pNext = 0;
    mv.multiviewGeometryShader = VK_FALSE;
    const char *exts[15];
    uint32_t nexts = 0;
    for (uint32_t i = 0; i < next && nexts < 14; i++)
        exts[nexts++] = ext[i];
    void *chain = mv.multiview ? &mv : 0;
    VkcPortabilityFeatures ps_en = { .sType = VKC_STYPE_PORTABILITY_SUBSET_FEATURES };
    if (device_ext_present(c->phys, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME)) {
        exts[nexts++] = VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME;
        c->tess_split_pass = c->has_tess;
        VkcPortabilityFeatures ps = { .sType = VKC_STYPE_PORTABILITY_SUBSET_FEATURES };
        VkPhysicalDeviceFeatures2 pf2 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &ps };
        vkGetPhysicalDeviceFeatures2(c->phys, &pf2);
        if (ps.mutableComparisonSamplers) {
            ps_en.mutableComparisonSamplers = VK_TRUE;
            ps_en.pNext = chain;
            chain = &ps_en;
        } else {
            plat_log("vkcore: mutableComparisonSamplers unsupported, shadow sampling may fail");
        }
    }
    VkDeviceCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = chain,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &qci,
        .enabledExtensionCount = nexts,
        .ppEnabledExtensionNames = exts,
        .pEnabledFeatures = &en,
    };
    if (vkc_device_hook)
        VKCHK(vkc_device_hook(c->phys, &ci, &c->device));
    else
        VKCHK(vkCreateDevice(c->phys, &ci, 0, &c->device));
    volkLoadDevice(c->device);
    vkGetDeviceQueue(c->device, c->qfam, 0, &c->queue);
    return 0;
}

int vkc_finish_setup(VkCore *c)
{
    VkCommandPoolCreateInfo pci = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = c->qfam,
    };
    VKCHK(vkCreateCommandPool(c->device, &pci, 0, &c->cmdpool));
    VkDescriptorPoolSize sizes[2] = {
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 32 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 256 },
    };
    VkDescriptorPoolCreateInfo dci = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
        .maxSets = 64,
        .poolSizeCount = 2,
        .pPoolSizes = sizes,
    };
    VKCHK(vkCreateDescriptorPool(c->device, &dci, 0, &c->dpool));
    VkPhysicalDeviceFeatures sup;
    vkGetPhysicalDeviceFeatures(c->phys, &sup);
    float aniso = c->props.limits.maxSamplerAnisotropy;
#ifdef __ANDROID__
    if (aniso > 2.0f) aniso = 2.0f;
#else
    if (aniso > 8.0f) aniso = 8.0f;
#endif
    VkSamplerCreateInfo sci = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .anisotropyEnable = sup.samplerAnisotropy,
        .maxAnisotropy = sup.samplerAnisotropy ? aniso : 1.0f,
        .maxLod = VK_LOD_CLAMP_NONE,
    };
    VKCHK(vkCreateSampler(c->device, &sci, 0, &c->sampler_repeat));
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.anisotropyEnable = VK_FALSE;
    sci.maxAnisotropy = 1.0f;
    VKCHK(vkCreateSampler(c->device, &sci, 0, &c->sampler_clamp));
    VkFormatProperties dfp;
    vkGetPhysicalDeviceFormatProperties(c->phys, VK_FORMAT_D16_UNORM, &dfp);
    VkFilter sfilter = (dfp.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)
        ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    sci.magFilter = sfilter;
    sci.minFilter = sfilter;
    sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    sci.compareEnable = VK_TRUE;
    sci.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    sci.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    sci.maxLod = 0.0f;
    VKCHK(vkCreateSampler(c->device, &sci, 0, &c->sampler_shadow));
    VkPipelineCacheCreateInfo cci = { .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };
    VKCHK(vkCreatePipelineCache(c->device, &cci, 0, &c->pcache));
    return 0;
}

void vkc_core_destroy(VkCore *c)
{
    if (!c) return;
    if (c->device) {
        VKWARN(vkDeviceWaitIdle(c->device));
        if (c->pcache) vkDestroyPipelineCache(c->device, c->pcache, 0);
        if (c->sampler_repeat) vkDestroySampler(c->device, c->sampler_repeat, 0);
        if (c->sampler_clamp) vkDestroySampler(c->device, c->sampler_clamp, 0);
        if (c->sampler_shadow) vkDestroySampler(c->device, c->sampler_shadow, 0);
        if (c->dpool) vkDestroyDescriptorPool(c->device, c->dpool, 0);
        if (c->cmdpool) vkDestroyCommandPool(c->device, c->cmdpool, 0);
        vkDestroyDevice(c->device, 0);
    }
    if (c->instance) vkDestroyInstance(c->instance, 0);
    memset(c, 0, sizeof *c);
}

static VkSpecializationMapEntry g_tier_entry = { 0, 0, sizeof(uint32_t) };
static uint32_t g_tier_value;
static VkSpecializationInfo g_tier_spec;

const VkSpecializationInfo *vkc_tier_spec(VkCore *c)
{
    g_tier_value = c->tier;
    g_tier_spec = (VkSpecializationInfo){ 1, &g_tier_entry, sizeof g_tier_value, &g_tier_value };
    return &g_tier_spec;
}

static int create_image(VkCore *c, uint32_t w, uint32_t h, uint32_t layers, uint32_t mips,
                        VkFormat fmt, VkSampleCountFlagBits samples, VkImageUsageFlags usage,
                        VkImageAspectFlags aspect, VkMemoryPropertyFlags want, VImg *out)
{
    memset(out, 0, sizeof *out);
    out->w = w;
    out->h = h;
    out->mips = mips;
    out->fmt = fmt;
    VkImageCreateInfo ici = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = fmt,
        .extent = { w, h, 1 },
        .mipLevels = mips,
        .arrayLayers = layers,
        .samples = samples,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VKCHK(vkCreateImage(c->device, &ici, 0, &out->img));
    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(c->device, out->img, &mr);
    int32_t mt = find_mem(c, mr.memoryTypeBits, want);
    if (mt < 0 && want != VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
        mt = find_mem(c, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (mt < 0)
        mt = find_mem(c, mr.memoryTypeBits, 0);
    if (mt < 0) return -1;
    VkMemoryAllocateInfo mai = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = mr.size,
        .memoryTypeIndex = (uint32_t)mt,
    };
    VKCHK(vkAllocateMemory(c->device, &mai, 0, &out->mem));
    VKCHK(vkBindImageMemory(c->device, out->img, out->mem, 0));
    VkImageViewCreateInfo vci = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = out->img,
        .viewType = layers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D,
        .format = fmt,
        .subresourceRange = { aspect, 0, mips, 0, layers },
    };
    VKCHK(vkCreateImageView(c->device, &vci, 0, &out->view));
    return 0;
}

int vkc_image_create(VkCore *c, uint32_t w, uint32_t h, uint32_t layers, uint32_t mips,
                     VkFormat fmt, VkSampleCountFlagBits samples, VkImageUsageFlags usage,
                     bool transient, VImg *out)
{
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) {
        aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
        if (fmt == VK_FORMAT_D24_UNORM_S8_UINT || fmt == VK_FORMAT_D32_SFLOAT_S8_UINT)
            aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    if (transient)
        usage |= VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
    return create_image(c, w, h, layers, mips, fmt, samples, usage, aspect,
                        transient ? VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                        out);
}

static const VkRenderPassMultiviewCreateInfo *multiview_info(uint32_t view_mask)
{
    static uint32_t vmask, cmask;
    static VkRenderPassMultiviewCreateInfo mvci;
    vmask = view_mask;
    cmask = view_mask;
    mvci = (VkRenderPassMultiviewCreateInfo){
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_MULTIVIEW_CREATE_INFO,
        .subpassCount = 1,
        .pViewMasks = &vmask,
        .correlationMaskCount = 1,
        .pCorrelationMasks = &cmask,
    };
    return &mvci;
}

static int scene_pass_make(Pass *p, VkCore *c, VkFormat fmt, VkImageView msaa_color, VkImageView msaa_depth,
                           VkImageView resolve, uint32_t w, uint32_t h, uint32_t view_mask,
                           bool load_prev, bool with_resolve)
{
    memset(p, 0, sizeof *p);
    p->core = c;
    p->width = w;
    p->height = h;
    VkAttachmentDescription at[3] = {
        {
            .format = fmt,
            .samples = c->msaa,
            .loadOp = load_prev ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = with_resolve ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = load_prev ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        },
        {
            .format = c->depth_format,
            .samples = c->msaa,
            .loadOp = load_prev ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = with_resolve ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = load_prev ? VK_ATTACHMENT_LOAD_OP_DONT_CARE : VK_ATTACHMENT_LOAD_OP_CLEAR,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = load_prev ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        },
        {
            .format = fmt,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        },
    };
    VkAttachmentReference cref = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference dref = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
    VkAttachmentReference rref = { 2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkSubpassDescription sp = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &cref,
        .pResolveAttachments = with_resolve ? &rref : 0,
        .pDepthStencilAttachment = &dref,
    };
    VkAccessFlags rw = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    VkAccessFlags rr = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    VkSubpassDependency deps[2] = {
        {
            .srcSubpass = VK_SUBPASS_EXTERNAL,
            .dstSubpass = 0,
            .srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            .srcAccessMask = load_prev ? rw : 0,
            .dstAccessMask = load_prev ? (rw | rr) : rw,
        },
        {
            .srcSubpass = 0,
            .dstSubpass = VK_SUBPASS_EXTERNAL,
            .srcStageMask = with_resolve
                ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                : VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            .dstStageMask = with_resolve
                ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                : VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
            .srcAccessMask = with_resolve ? VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT : rw,
            .dstAccessMask = with_resolve ? VK_ACCESS_SHADER_READ_BIT : (rw | rr),
        },
    };
    VkRenderPassCreateInfo rpci = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .pNext = view_mask ? multiview_info(view_mask) : 0,
        .attachmentCount = with_resolve ? 3u : 2u,
        .pAttachments = at,
        .subpassCount = 1,
        .pSubpasses = &sp,
        .dependencyCount = 2,
        .pDependencies = deps,
    };
    VKCHK(vkCreateRenderPass(c->device, &rpci, 0, &p->rp));
    VkImageView atts[3] = { msaa_color, msaa_depth, resolve };
    VkFramebufferCreateInfo fci = {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = p->rp,
        .attachmentCount = with_resolve ? 3u : 2u,
        .pAttachments = atts,
        .width = w,
        .height = h,
        .layers = 1,
    };
    VKCHK(vkCreateFramebuffer(c->device, &fci, 0, &p->fb));
    return 0;
}

int vkc_pass_scene(Pass *p, VkCore *c, VkFormat fmt, VkImageView msaa_color, VkImageView msaa_depth,
                   VkImageView resolve, uint32_t w, uint32_t h, uint32_t view_mask)
{
    return scene_pass_make(p, c, fmt, msaa_color, msaa_depth, resolve, w, h, view_mask, false, true);
}

int vkc_pass_scene_pre(Pass *p, VkCore *c, VkFormat fmt, VkImageView msaa_color, VkImageView msaa_depth,
                       uint32_t w, uint32_t h, uint32_t view_mask)
{
    return scene_pass_make(p, c, fmt, msaa_color, msaa_depth, VK_NULL_HANDLE, w, h, view_mask, false, false);
}

int vkc_pass_scene_main(Pass *p, VkCore *c, VkFormat fmt, VkImageView msaa_color, VkImageView msaa_depth,
                        VkImageView resolve, uint32_t w, uint32_t h, uint32_t view_mask)
{
    return scene_pass_make(p, c, fmt, msaa_color, msaa_depth, resolve, w, h, view_mask, true, true);
}

int vkc_pass_color(Pass *p, VkCore *c, VkFormat fmt, VkImageView target, uint32_t w, uint32_t h,
                   uint32_t view_mask, bool load_existing)
{
    memset(p, 0, sizeof *p);
    p->core = c;
    p->width = w;
    p->height = h;
    VkAttachmentDescription at = {
        .format = fmt,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = load_existing ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = load_existing ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    VkAttachmentReference cref = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkSubpassDescription sp = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &cref,
    };
    VkSubpassDependency deps[2] = {
        {
            .srcSubpass = VK_SUBPASS_EXTERNAL,
            .dstSubpass = 0,
            .srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = load_existing ? VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT : 0,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,
        },
        {
            .srcSubpass = 0,
            .dstSubpass = VK_SUBPASS_EXTERNAL,
            .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        },
    };
    VkRenderPassCreateInfo rpci = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .pNext = view_mask ? multiview_info(view_mask) : 0,
        .attachmentCount = 1,
        .pAttachments = &at,
        .subpassCount = 1,
        .pSubpasses = &sp,
        .dependencyCount = 2,
        .pDependencies = deps,
    };
    VKCHK(vkCreateRenderPass(c->device, &rpci, 0, &p->rp));
    VkFramebufferCreateInfo fci = {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = p->rp,
        .attachmentCount = 1,
        .pAttachments = &target,
        .width = w,
        .height = h,
        .layers = 1,
    };
    VKCHK(vkCreateFramebuffer(c->device, &fci, 0, &p->fb));
    return 0;
}

int vkc_pass_depth(Pass *p, VkCore *c, VkFormat fmt, VkImageView target, uint32_t w, uint32_t h)
{
    memset(p, 0, sizeof *p);
    p->core = c;
    p->width = w;
    p->height = h;
    VkAttachmentDescription at = {
        .format = fmt,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    VkAttachmentReference dref = { 0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
    VkSubpassDescription sp = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .pDepthStencilAttachment = &dref,
    };
    VkSubpassDependency deps[2] = {
        {
            .srcSubpass = VK_SUBPASS_EXTERNAL,
            .dstSubpass = 0,
            .srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        },
        {
            .srcSubpass = 0,
            .dstSubpass = VK_SUBPASS_EXTERNAL,
            .srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            .srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        },
    };
    VkRenderPassCreateInfo rpci = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &at,
        .subpassCount = 1,
        .pSubpasses = &sp,
        .dependencyCount = 2,
        .pDependencies = deps,
    };
    VKCHK(vkCreateRenderPass(c->device, &rpci, 0, &p->rp));
    VkFramebufferCreateInfo fci = {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = p->rp,
        .attachmentCount = 1,
        .pAttachments = &target,
        .width = w,
        .height = h,
        .layers = 1,
    };
    VKCHK(vkCreateFramebuffer(c->device, &fci, 0, &p->fb));
    return 0;
}

int vkc_pipe_depth(VkCore *c, VkRenderPass rp, VkPipelineLayout layout, const char *vs_asset,
                   const VkVertexInputBindingDescription *vb, uint32_t nvb,
                   const VkVertexInputAttributeDescription *va, uint32_t nva, VkPipeline *out)
{
    VkShaderModule vs = vkc_shader(c, vs_asset);
    if (vs == VK_NULL_HANDLE) return -1;
    VkPipelineShaderStageCreateInfo stage = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .module = vs,
        .pName = "main",
        .pSpecializationInfo = vkc_tier_spec(c),
    };
    VkPipelineVertexInputStateCreateInfo vin = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = nvb, .pVertexBindingDescriptions = vb,
        .vertexAttributeDescriptionCount = nva, .pVertexAttributeDescriptions = va,
    };
    VkPipelineInputAssemblyStateCreateInfo ia = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    VkPipelineViewportStateCreateInfo vps = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };
    VkPipelineRasterizationStateCreateInfo rs = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .depthBiasEnable = VK_TRUE,
        .depthBiasConstantFactor = 4.0f,
        .depthBiasSlopeFactor = 1.75f,
        .lineWidth = 1.0f,
    };
    VkPipelineMultisampleStateCreateInfo ms = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    VkPipelineDepthStencilStateCreateInfo ds = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
    };
    VkPipelineColorBlendStateCreateInfo cb = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
    };
    VkDynamicState dyn[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dsi = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2,
        .pDynamicStates = dyn,
    };
    VkGraphicsPipelineCreateInfo pci = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 1,
        .pStages = &stage,
        .pVertexInputState = &vin,
        .pInputAssemblyState = &ia,
        .pViewportState = &vps,
        .pRasterizationState = &rs,
        .pMultisampleState = &ms,
        .pDepthStencilState = &ds,
        .pColorBlendState = &cb,
        .pDynamicState = &dsi,
        .layout = layout,
        .renderPass = rp,
        .subpass = 0,
    };
    VkResult r = vkCreateGraphicsPipelines(c->device, c->pcache, 1, &pci, 0, out);
    vkDestroyShaderModule(c->device, vs, 0);
    if (r != VK_SUCCESS) {
        plat_log("vkcore: depth pipeline %s failed (%d)", vs_asset, (int)r);
        return -1;
    }
    return 0;
}

void vkc_pass_begin(VkCommandBuffer cmd, const Pass *p, const VkClearValue *clears, uint32_t nclears)
{
    VkRenderPassBeginInfo rbi = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = p->rp,
        .framebuffer = p->fb,
        .renderArea = { { 0, 0 }, { p->width, p->height } },
        .clearValueCount = nclears,
        .pClearValues = clears,
    };
    vkCmdBeginRenderPass(cmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);
}

void vkc_pass_destroy(Pass *p)
{
    VkCore *c = p->core;
    if (!c || !c->device) return;
    if (p->fb) vkDestroyFramebuffer(c->device, p->fb, 0);
    if (p->rp) vkDestroyRenderPass(c->device, p->rp, 0);
    memset(p, 0, sizeof *p);
}

int vkc_targets_create(RenderTargets *rt, VkCore *c, uint32_t w, uint32_t h, uint32_t layers,
                       VkFormat fmt, VkImage *images, uint32_t count, VkImageLayout final_layout)
{
    memset(rt, 0, sizeof *rt);
    if (count > 8 || !count || !layers) return -1;
    rt->core = c;
    rt->width = w;
    rt->height = h;
    rt->layers = layers;
    rt->color_format = fmt;
    rt->target_count = count;
    VkAttachmentDescription at = {
        .format = fmt,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = final_layout,
    };
    VkAttachmentReference cref = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkSubpassDescription sp = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &cref,
    };
    VkSubpassDependency deps[2] = {
        {
            .srcSubpass = VK_SUBPASS_EXTERNAL,
            .dstSubpass = 0,
            .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        },
        {
            .srcSubpass = 0,
            .dstSubpass = VK_SUBPASS_EXTERNAL,
            .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dstAccessMask = 0,
        },
    };
    VkRenderPassCreateInfo rpci = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .pNext = layers == 2 ? multiview_info(0x3) : 0,
        .attachmentCount = 1,
        .pAttachments = &at,
        .subpassCount = 1,
        .pSubpasses = &sp,
        .dependencyCount = 2,
        .pDependencies = deps,
    };
    VKCHK(vkCreateRenderPass(c->device, &rpci, 0, &rt->rp));
    for (uint32_t i = 0; i < count; i++) {
        rt->target_images[i] = images[i];
        VkImageViewCreateInfo vci = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = images[i],
            .viewType = layers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D,
            .format = fmt,
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, layers },
        };
        VKCHK(vkCreateImageView(c->device, &vci, 0, &rt->target_views[i]));
        VkFramebufferCreateInfo fci = {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass = rt->rp,
            .attachmentCount = 1,
            .pAttachments = &rt->target_views[i],
            .width = w,
            .height = h,
            .layers = 1,
        };
        VKCHK(vkCreateFramebuffer(c->device, &fci, 0, &rt->fbs[i]));
    }
    return 0;
}

void vkc_targets_destroy(RenderTargets *rt)
{
    VkCore *c = rt->core;
    if (!c || !c->device) return;
    for (uint32_t i = 0; i < rt->target_count; i++) {
        if (rt->fbs[i]) vkDestroyFramebuffer(c->device, rt->fbs[i], 0);
        if (rt->target_views[i]) vkDestroyImageView(c->device, rt->target_views[i], 0);
    }
    if (rt->rp) vkDestroyRenderPass(c->device, rt->rp, 0);
    memset(rt, 0, sizeof *rt);
}

static int flatchain_build(FlatChain *fc, uint32_t w, uint32_t h)
{
    VkCore *c = fc->core;
    VkSurfaceCapabilitiesKHR caps;
    VKCHK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(c->phys, fc->surface, &caps));
    uint32_t ew = caps.currentExtent.width;
    uint32_t eh = caps.currentExtent.height;
    if (ew == UINT32_MAX) {
        ew = w;
        eh = h;
        if (ew < caps.minImageExtent.width) ew = caps.minImageExtent.width;
        if (ew > caps.maxImageExtent.width) ew = caps.maxImageExtent.width;
        if (eh < caps.minImageExtent.height) eh = caps.minImageExtent.height;
        if (eh > caps.maxImageExtent.height) eh = caps.maxImageExtent.height;
    }
    if (!ew || !eh) return 1;
    uint32_t nf = 0;
    VKCHK(vkGetPhysicalDeviceSurfaceFormatsKHR(c->phys, fc->surface, &nf, 0));
    if (!nf) return -1;
    if (nf > 64) nf = 64;
    VkSurfaceFormatKHR fmts[64];
    VkResult fr = vkGetPhysicalDeviceSurfaceFormatsKHR(c->phys, fc->surface, &nf, fmts);
    if (fr != VK_SUCCESS && fr != VK_INCOMPLETE) return -1;
    VkSurfaceFormatKHR pick = fmts[0];
    for (uint32_t i = 0; i < nf; i++)
        if ((fmts[i].format == VK_FORMAT_B8G8R8A8_SRGB || fmts[i].format == VK_FORMAT_R8G8B8A8_SRGB) &&
            fmts[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            pick = fmts[i];
            break;
        }
    uint32_t imgc = 3;
    if (imgc < caps.minImageCount) imgc = caps.minImageCount;
    if (caps.maxImageCount && imgc > caps.maxImageCount) imgc = caps.maxImageCount;
    VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
        usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    fc->transfer_src = (usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;
    VkCompositeAlphaFlagBitsKHR calpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (!(caps.supportedCompositeAlpha & calpha)) {
        for (uint32_t bit = 1; bit <= 8; bit <<= 1)
            if (caps.supportedCompositeAlpha & bit) {
                calpha = (VkCompositeAlphaFlagBitsKHR)bit;
                break;
            }
    }
    VkSwapchainCreateInfoKHR sci = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = fc->surface,
        .minImageCount = imgc,
        .imageFormat = pick.format,
        .imageColorSpace = pick.colorSpace,
        .imageExtent = { ew, eh },
        .imageArrayLayers = 1,
        .imageUsage = usage,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = caps.currentTransform,
        .compositeAlpha = calpha,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR,
        .clipped = VK_TRUE,
        .oldSwapchain = fc->swapchain,
    };
    VkSwapchainKHR ns = VK_NULL_HANDLE;
    VKCHK(vkCreateSwapchainKHR(c->device, &sci, 0, &ns));
    if (fc->swapchain) vkDestroySwapchainKHR(c->device, fc->swapchain, 0);
    fc->swapchain = ns;
    uint32_t n = 0;
    VKCHK(vkGetSwapchainImagesKHR(c->device, fc->swapchain, &n, 0));
    if (!n || n > 8) return -1;
    VkImage imgs[8];
    VKCHK(vkGetSwapchainImagesKHR(c->device, fc->swapchain, &n, imgs));
    if (vkc_targets_create(&fc->rt, c, ew, eh, 1, pick.format, imgs, n, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR))
        return -1;
    return 0;
}

static int flatchain_recreate(FlatChain *fc)
{
    VkCore *c = fc->core;
    VKCHK(vkDeviceWaitIdle(c->device));
    uint32_t w = fc->rt.width;
    uint32_t h = fc->rt.height;
    vkc_targets_destroy(&fc->rt);
    int r = flatchain_build(fc, w, h);
    if (r < 0) return -1;
    if (fc->rt.rp && render_resize(fc->rt.width, fc->rt.height))
        return -1;
    return 0;
}

int vkc_flatchain_create(FlatChain *fc, VkCore *c, VkSurfaceKHR surface, uint32_t w, uint32_t h)
{
    memset(fc, 0, sizeof *fc);
    fc->core = c;
    fc->surface = surface;
    VkBool32 present = VK_FALSE;
    VKCHK(vkGetPhysicalDeviceSurfaceSupportKHR(c->phys, c->qfam, surface, &present));
    if (!present) {
        plat_log("vkcore: queue family %u cannot present to surface", c->qfam);
        return -1;
    }
    if (flatchain_build(fc, w, h)) return -1;
    VkCommandBufferAllocateInfo cai = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = c->cmdpool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = VISTA_FRAMES,
    };
    VKCHK(vkAllocateCommandBuffers(c->device, &cai, fc->cmd));
    for (uint32_t i = 0; i < VISTA_FRAMES; i++) {
        VkFenceCreateInfo fi = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT };
        VKCHK(vkCreateFence(c->device, &fi, 0, &fc->fence[i]));
        VkSemaphoreCreateInfo si = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        VKCHK(vkCreateSemaphore(c->device, &si, 0, &fc->sem_acquire[i]));
    }
    for (uint32_t i = 0; i < 8; i++) {
        VkSemaphoreCreateInfo si = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        VKCHK(vkCreateSemaphore(c->device, &si, 0, &fc->sem_release[i]));
    }
    return 0;
}

void vkc_flatchain_destroy(FlatChain *fc)
{
    VkCore *c = fc->core;
    if (!c || !c->device) return;
    VKWARN(vkDeviceWaitIdle(c->device));
    vkc_targets_destroy(&fc->rt);
    if (fc->swapchain) vkDestroySwapchainKHR(c->device, fc->swapchain, 0);
    for (uint32_t i = 0; i < VISTA_FRAMES; i++) {
        if (fc->fence[i]) vkDestroyFence(c->device, fc->fence[i], 0);
        if (fc->sem_acquire[i]) vkDestroySemaphore(c->device, fc->sem_acquire[i], 0);
        if (fc->cmd[i]) vkFreeCommandBuffers(c->device, c->cmdpool, 1, &fc->cmd[i]);
    }
    for (uint32_t i = 0; i < 8; i++)
        if (fc->sem_release[i]) vkDestroySemaphore(c->device, fc->sem_release[i], 0);
    memset(fc, 0, sizeof *fc);
}

static void img_barrier(VkCommandBuffer cmd, VkImage img, VkImageAspectFlags aspect,
                        uint32_t basemip, uint32_t mips,
                        VkImageLayout from, VkImageLayout to,
                        VkAccessFlags sa, VkAccessFlags da,
                        VkPipelineStageFlags ss, VkPipelineStageFlags ds);

static int flatchain_frame_ex(FlatChain *fc, const FrameUBO *ubo, VBuf *shot, bool *shot_done)
{
    VkCore *c = fc->core;
    uint32_t s = fc->slot;
    VKCHK(vkWaitForFences(c->device, 1, &fc->fence[s], VK_TRUE, UINT64_MAX));
    if (!fc->swapchain || !fc->rt.rp) {
        if (flatchain_recreate(fc)) return -1;
        if (!fc->rt.rp) return 0;
    }
    uint32_t idx = 0;
    VkResult ar = vkAcquireNextImageKHR(c->device, fc->swapchain, UINT64_MAX, fc->sem_acquire[s], VK_NULL_HANDLE, &idx);
    if (ar == VK_ERROR_OUT_OF_DATE_KHR) {
        if (flatchain_recreate(fc)) return -1;
        return 0;
    }
    if (ar != VK_SUCCESS && ar != VK_SUBOPTIMAL_KHR) {
        plat_log("vkcore: acquire failed (%d)", (int)ar);
        return -1;
    }
    VKCHK(vkResetFences(c->device, 1, &fc->fence[s]));
    veg_update((v3){ ubo->campos[0], ubo->campos[1], ubo->campos[2] }, s);
    scene_update_ubo(s, ubo);
    bool capture = shot && fc->transfer_src &&
                   shot->size >= (VkDeviceSize)fc->rt.width * fc->rt.height * 4;
    VkCommandBuffer cmd = fc->cmd[s];
    VkCommandBufferBeginInfo bi = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    VKCHK(vkBeginCommandBuffer(cmd, &bi));
    render_record(cmd, s, fc->rt.fbs[idx], fc->rt.width, fc->rt.height);
    if (capture) {
        VkImage img = fc->rt.target_images[idx];
        img_barrier(cmd, img, VK_IMAGE_ASPECT_COLOR_BIT, 0, 1,
                    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkBufferImageCopy region = {
            .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .imageExtent = { fc->rt.width, fc->rt.height, 1 },
        };
        vkCmdCopyImageToBuffer(cmd, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, shot->buf, 1, &region);
        img_barrier(cmd, img, VK_IMAGE_ASPECT_COLOR_BIT, 0, 1,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                    VK_ACCESS_TRANSFER_READ_BIT, 0,
                    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    }
    VKCHK(vkEndCommandBuffer(cmd));
    VkPipelineStageFlags ws = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &fc->sem_acquire[s],
        .pWaitDstStageMask = &ws,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &fc->sem_release[idx],
    };
    VKCHK(vkQueueSubmit(c->queue, 1, &si, fc->fence[s]));
    VkPresentInfoKHR pi = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &fc->sem_release[idx],
        .swapchainCount = 1,
        .pSwapchains = &fc->swapchain,
        .pImageIndices = &idx,
    };
    VkResult pr = vkQueuePresentKHR(c->queue, &pi);
    fc->slot = (s + 1) % VISTA_FRAMES;
    if (capture) {
        VKCHK(vkWaitForFences(c->device, 1, &fc->fence[s], VK_TRUE, UINT64_MAX));
        if (shot_done) *shot_done = true;
    }
    if (pr == VK_ERROR_OUT_OF_DATE_KHR || pr == VK_SUBOPTIMAL_KHR) {
        if (flatchain_recreate(fc)) return -1;
        return 0;
    }
    if (pr != VK_SUCCESS) {
        plat_log("vkcore: present failed (%d)", (int)pr);
        return -1;
    }
    return 0;
}

int vkc_flatchain_frame(FlatChain *fc, const FrameUBO *ubo)
{
    return flatchain_frame_ex(fc, ubo, NULL, NULL);
}

static void img_barrier(VkCommandBuffer cmd, VkImage img, VkImageAspectFlags aspect,
                        uint32_t basemip, uint32_t mips,
                        VkImageLayout from, VkImageLayout to,
                        VkAccessFlags sa, VkAccessFlags da,
                        VkPipelineStageFlags ss, VkPipelineStageFlags ds)
{
    VkImageMemoryBarrier b = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = sa,
        .dstAccessMask = da,
        .oldLayout = from,
        .newLayout = to,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = img,
        .subresourceRange = { aspect, basemip, mips, 0, VK_REMAINING_ARRAY_LAYERS },
    };
    vkCmdPipelineBarrier(cmd, ss, ds, 0, 0, 0, 0, 0, 1, &b);
}

static int png_write_rgba(uint8_t *px, uint32_t w, uint32_t h, bool bgr, const char *path)
{
    for (uint32_t i = 0; i < w * h; i++) {
        if (bgr) {
            uint8_t t = px[i * 4 + 0];
            px[i * 4 + 0] = px[i * 4 + 2];
            px[i * 4 + 2] = t;
        }
        px[i * 4 + 3] = 255;
    }
    if (!stbi_write_png(path, (int)w, (int)h, 4, px, (int)(w * 4))) {
        plat_log("vkcore: png write failed %s", path);
        return -1;
    }
    return 0;
}

int vkc_flatchain_screenshot(FlatChain *fc, const FrameUBO *ubo, const char *path)
{
    VkCore *c = fc->core;
    for (int attempt = 0; attempt < 8; attempt++) {
        if (!fc->rt.rp || !fc->rt.width || !fc->rt.height) {
            if (vkc_flatchain_frame(fc, ubo)) return -1;
            continue;
        }
        if (!fc->transfer_src) {
            plat_log("vkcore: swapchain lacks transfer-src usage, cannot screenshot");
            return -1;
        }
        uint32_t w = fc->rt.width;
        uint32_t h = fc->rt.height;
        VBuf hb;
        if (vkc_buffer(c, (VkDeviceSize)w * h * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &hb))
            return -1;
        bool done = false;
        if (flatchain_frame_ex(fc, ubo, &hb, &done)) {
            vkc_buffer_destroy(c, &hb);
            return -1;
        }
        if (!done) {
            vkc_buffer_destroy(c, &hb);
            continue;
        }
        uint8_t *px = (uint8_t *)hb.map;
        bool bgr = fc->rt.color_format == VK_FORMAT_B8G8R8A8_SRGB || fc->rt.color_format == VK_FORMAT_B8G8R8A8_UNORM;
        int ok = png_write_rgba(px, w, h, bgr, path);
        vkc_buffer_destroy(c, &hb);
        if (ok)
            return -1;
        return 0;
    }
    plat_log("vkcore: screenshot could not acquire a frame");
    return -1;
}

int vkc_offchain_create(OffChain *oc, VkCore *c, uint32_t w, uint32_t h)
{
    memset(oc, 0, sizeof *oc);
    oc->core = c;
    if (create_image(c, w, h, 1, 1, VK_FORMAT_R8G8B8A8_SRGB, VK_SAMPLE_COUNT_1_BIT,
                     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                     VK_IMAGE_ASPECT_COLOR_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &oc->color))
        return -1;
    if (vkc_targets_create(&oc->rt, c, w, h, 1, VK_FORMAT_R8G8B8A8_SRGB, &oc->color.img, 1,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL))
        return -1;
    VkCommandBufferAllocateInfo cai = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = c->cmdpool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = VISTA_FRAMES,
    };
    VKCHK(vkAllocateCommandBuffers(c->device, &cai, oc->cmd));
    for (uint32_t i = 0; i < VISTA_FRAMES; i++) {
        VkFenceCreateInfo fi = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT };
        VKCHK(vkCreateFence(c->device, &fi, 0, &oc->fence[i]));
    }
    return 0;
}

void vkc_offchain_destroy(OffChain *oc)
{
    VkCore *c = oc->core;
    if (!c || !c->device) return;
    VKWARN(vkDeviceWaitIdle(c->device));
    vkc_targets_destroy(&oc->rt);
    vkc_image_destroy(c, &oc->color);
    for (uint32_t i = 0; i < VISTA_FRAMES; i++) {
        if (oc->fence[i]) vkDestroyFence(c->device, oc->fence[i], 0);
        if (oc->cmd[i]) vkFreeCommandBuffers(c->device, c->cmdpool, 1, &oc->cmd[i]);
    }
    memset(oc, 0, sizeof *oc);
}

static int offchain_frame_ex(OffChain *oc, const FrameUBO *ubo, VBuf *shot)
{
    VkCore *c = oc->core;
    uint32_t s = oc->slot;
    VKCHK(vkWaitForFences(c->device, 1, &oc->fence[s], VK_TRUE, UINT64_MAX));
    VKCHK(vkResetFences(c->device, 1, &oc->fence[s]));
    veg_update((v3){ ubo->campos[0], ubo->campos[1], ubo->campos[2] }, s);
    scene_update_ubo(s, ubo);
    VkCommandBuffer cmd = oc->cmd[s];
    VkCommandBufferBeginInfo bi = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    VKCHK(vkBeginCommandBuffer(cmd, &bi));
    render_record(cmd, s, oc->rt.fbs[0], oc->rt.width, oc->rt.height);
    if (shot) {
        img_barrier(cmd, oc->color.img, VK_IMAGE_ASPECT_COLOR_BIT, 0, 1,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkBufferImageCopy region = {
            .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .imageExtent = { oc->rt.width, oc->rt.height, 1 },
        };
        vkCmdCopyImageToBuffer(cmd, oc->color.img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, shot->buf, 1, &region);
    }
    VKCHK(vkEndCommandBuffer(cmd));
    VkSubmitInfo si = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd,
    };
    VKCHK(vkQueueSubmit(c->queue, 1, &si, oc->fence[s]));
    oc->slot = (s + 1) % VISTA_FRAMES;
    if (shot)
        VKCHK(vkWaitForFences(c->device, 1, &oc->fence[s], VK_TRUE, UINT64_MAX));
    return 0;
}

int vkc_offchain_frame(OffChain *oc, const FrameUBO *ubo)
{
    return offchain_frame_ex(oc, ubo, NULL);
}

int vkc_offchain_screenshot(OffChain *oc, const FrameUBO *ubo, const char *path)
{
    VkCore *c = oc->core;
    VBuf hb;
    if (vkc_buffer(c, (VkDeviceSize)oc->rt.width * oc->rt.height * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &hb))
        return -1;
    if (offchain_frame_ex(oc, ubo, &hb)) {
        vkc_buffer_destroy(c, &hb);
        return -1;
    }
    int rc = png_write_rgba((uint8_t *)hb.map, oc->rt.width, oc->rt.height, false, path);
    vkc_buffer_destroy(c, &hb);
    return rc;
}

int vkc_buffer(VkCore *c, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props, VBuf *out)
{
    memset(out, 0, sizeof *out);
    out->size = size;
    VkBufferCreateInfo bi = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VKCHK(vkCreateBuffer(c->device, &bi, 0, &out->buf));
    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(c->device, out->buf, &mr);
    int32_t mt = find_mem(c, mr.memoryTypeBits, props);
    if (mt < 0) return -1;
    VkMemoryAllocateInfo mai = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = mr.size,
        .memoryTypeIndex = (uint32_t)mt,
    };
    VKCHK(vkAllocateMemory(c->device, &mai, 0, &out->mem));
    VKCHK(vkBindBufferMemory(c->device, out->buf, out->mem, 0));
    if (props & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
        VKCHK(vkMapMemory(c->device, out->mem, 0, VK_WHOLE_SIZE, 0, &out->map));
    return 0;
}

void vkc_buffer_destroy(VkCore *c, VBuf *b)
{
    if (!c || !c->device) return;
    if (b->map) vkUnmapMemory(c->device, b->mem);
    if (b->buf) vkDestroyBuffer(c->device, b->buf, 0);
    if (b->mem) vkFreeMemory(c->device, b->mem, 0);
    memset(b, 0, sizeof *b);
}

static int texture_upload(VkCore *c, const void *px, VkDeviceSize bytes, uint32_t w, uint32_t h,
                          VkFormat fmt, bool genmips, VImg *out)
{
    uint32_t mips = 1;
    if (genmips) {
        uint32_t m = w > h ? w : h;
        while (m > 1) {
            mips++;
            m >>= 1;
        }
    }
    VBuf st;
    if (vkc_buffer(c, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &st))
        return -1;
    memcpy(st.map, px, (size_t)bytes);
    VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (genmips) usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (create_image(c, w, h, 1, mips, fmt, VK_SAMPLE_COUNT_1_BIT, usage,
                     VK_IMAGE_ASPECT_COLOR_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, out)) {
        vkc_buffer_destroy(c, &st);
        return -1;
    }
    VkCommandBuffer cmd = vkc_begin_once(c);
    if (!cmd) {
        vkc_buffer_destroy(c, &st);
        return -1;
    }
    img_barrier(cmd, out->img, VK_IMAGE_ASPECT_COLOR_BIT, 0, mips,
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                0, VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkBufferImageCopy region = {
        .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .imageExtent = { w, h, 1 },
    };
    vkCmdCopyBufferToImage(cmd, st.buf, out->img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    if (genmips) {
        uint32_t mw = w;
        uint32_t mh = h;
        for (uint32_t i = 1; i < mips; i++) {
            uint32_t nw = mw > 1 ? mw / 2 : 1;
            uint32_t nh = mh > 1 ? mh / 2 : 1;
            img_barrier(cmd, out->img, VK_IMAGE_ASPECT_COLOR_BIT, i - 1, 1,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
            VkImageBlit bl = {
                .srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, i - 1, 0, 1 },
                .srcOffsets = { { 0, 0, 0 }, { (int32_t)mw, (int32_t)mh, 1 } },
                .dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, i, 0, 1 },
                .dstOffsets = { { 0, 0, 0 }, { (int32_t)nw, (int32_t)nh, 1 } },
            };
            vkCmdBlitImage(cmd, out->img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           out->img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bl, VK_FILTER_LINEAR);
            img_barrier(cmd, out->img, VK_IMAGE_ASPECT_COLOR_BIT, i - 1, 1,
                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
            mw = nw;
            mh = nh;
        }
        img_barrier(cmd, out->img, VK_IMAGE_ASPECT_COLOR_BIT, mips - 1, 1,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
    } else {
        img_barrier(cmd, out->img, VK_IMAGE_ASPECT_COLOR_BIT, 0, 1,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
    }
    vkc_end_once(c, cmd);
    vkc_buffer_destroy(c, &st);
    return 0;
}

int vkc_texture_rgba(VkCore *c, const uint8_t *px, uint32_t w, uint32_t h, bool srgb, bool genmips, VImg *out)
{
    VkFormat fmt = srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
    return texture_upload(c, px, (VkDeviceSize)w * h * 4, w, h, fmt, genmips, out);
}

int vkc_texture_r16(VkCore *c, const uint16_t *px, uint32_t w, uint32_t h, VImg *out)
{
    return texture_upload(c, px, (VkDeviceSize)w * h * 2, w, h, VK_FORMAT_R16_UNORM, false, out);
}

void vkc_image_destroy(VkCore *c, VImg *i)
{
    if (!c || !c->device) return;
    if (i->view) vkDestroyImageView(c->device, i->view, 0);
    if (i->img) vkDestroyImage(c->device, i->img, 0);
    if (i->mem) vkFreeMemory(c->device, i->mem, 0);
    memset(i, 0, sizeof *i);
}

VkShaderModule vkc_shader(VkCore *c, const char *asset_name)
{
    int size = 0;
    uint8_t *data = plat_load_asset(asset_name, &size);
    if (!data || size <= 0 || (size & 3)) {
        plat_log("vkcore: shader asset load failed %s", asset_name);
        free(data);
        return VK_NULL_HANDLE;
    }
    VkShaderModuleCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = (size_t)size,
        .pCode = (const uint32_t *)(void *)data,
    };
    VkShaderModule m = VK_NULL_HANDLE;
    VkResult r = vkCreateShaderModule(c->device, &ci, 0, &m);
    free(data);
    if (r != VK_SUCCESS) {
        plat_log("vkcore: vkCreateShaderModule failed (%d) %s", (int)r, asset_name);
        return VK_NULL_HANDLE;
    }
    return m;
}

VkCommandBuffer vkc_begin_once(VkCore *c)
{
    VkCommandBufferAllocateInfo ai = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = c->cmdpool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(c->device, &ai, &cmd) != VK_SUCCESS) {
        plat_log("vkcore: one-shot alloc failed");
        return VK_NULL_HANDLE;
    }
    VkCommandBufferBeginInfo bi = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    if (vkBeginCommandBuffer(cmd, &bi) != VK_SUCCESS) {
        plat_log("vkcore: one-shot begin failed");
        vkFreeCommandBuffers(c->device, c->cmdpool, 1, &cmd);
        return VK_NULL_HANDLE;
    }
    return cmd;
}

void vkc_end_once(VkCore *c, VkCommandBuffer cmd)
{
    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        plat_log("vkcore: one-shot end failed");
        vkFreeCommandBuffers(c->device, c->cmdpool, 1, &cmd);
        return;
    }
    VkFenceCreateInfo fi = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VkFence f = VK_NULL_HANDLE;
    if (vkCreateFence(c->device, &fi, 0, &f) == VK_SUCCESS) {
        VkSubmitInfo si = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .commandBufferCount = 1,
            .pCommandBuffers = &cmd,
        };
        if (vkQueueSubmit(c->queue, 1, &si, f) == VK_SUCCESS)
            VKWARN(vkWaitForFences(c->device, 1, &f, VK_TRUE, UINT64_MAX));
        else
            plat_log("vkcore: one-shot submit failed");
        vkDestroyFence(c->device, f, 0);
    } else {
        plat_log("vkcore: one-shot fence create failed");
    }
    vkFreeCommandBuffers(c->device, c->cmdpool, 1, &cmd);
}
