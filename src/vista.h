#ifndef VISTA_H
#define VISTA_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <math.h>

#define VK_NO_PROTOTYPES
#if defined(_WIN32) && !defined(VK_USE_PLATFORM_WIN32_KHR)
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#if defined(__ANDROID__) && !defined(VK_USE_PLATFORM_ANDROID_KHR)
#define VK_USE_PLATFORM_ANDROID_KHR
#endif
#include "volk.h"

#define VISTA_FRAMES 2
#define TERRAIN_N 2048
#define TERRAIN_SCALE 4000.0f
#define TERRAIN_HEIGHT 380.0f
#define TERRAIN_CHUNKS 32
#define EYE_HEIGHT 1.7f

typedef struct { float x, y, z; } v3;
typedef struct { float x, y, z, w; } q4;
typedef struct { float m[16]; } m4;

static inline v3 v3add(v3 a, v3 b){ return (v3){a.x+b.x,a.y+b.y,a.z+b.z}; }
static inline v3 v3sub(v3 a, v3 b){ return (v3){a.x-b.x,a.y-b.y,a.z-b.z}; }
static inline v3 v3scale(v3 a, float s){ return (v3){a.x*s,a.y*s,a.z*s}; }
static inline float v3dot(v3 a, v3 b){ return a.x*b.x+a.y*b.y+a.z*b.z; }
static inline v3 v3cross(v3 a, v3 b){ return (v3){a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
static inline float v3len(v3 a){ return sqrtf(v3dot(a,a)); }
static inline v3 v3norm(v3 a){ float l=v3len(a); return l>1e-8f?v3scale(a,1.0f/l):(v3){0,0,0}; }

m4 m4identity(void);
m4 m4mul(m4 a, m4 b);
m4 m4perspective(float fovy, float aspect, float znear, float zfar);
m4 m4frustum_xr(float angleLeft, float angleRight, float angleUp, float angleDown, float znear, float zfar);
m4 m4ortho(float l, float r, float b, float t, float n, float f);
m4 m4look(v3 eye, v3 center, v3 up);
m4 m4from_rt(q4 rot, v3 pos);
m4 m4inverse_rt(m4 a);
v3 q4rotate(q4 q, v3 v);

typedef struct {
    float move_x, move_y;
    float look_dx, look_dy;
    bool jump, sprint, quit;
    float snap_turn;
} Input;

typedef struct {
    m4 viewproj[2];
    float campos[4];
    float sundir[4];
    float sun_radiance[4];
    float ambient_zenith[4];
    float ambient_horizon[4];
    float timeparams[4];
    float post[4];
    m4 cascade_vp[3];
    float cascade_radii[4];
    float cascade_texel[4];
} FrameUBO;

typedef struct {
    m4 vp;
    float chunk[4];
} ShadowPC;

typedef struct {
    VkBuffer buf;
    VkDeviceMemory mem;
    void *map;
    VkDeviceSize size;
} VBuf;

typedef struct {
    VkImage img;
    VkDeviceMemory mem;
    VkImageView view;
    uint32_t w, h, mips;
    VkFormat fmt;
} VImg;

typedef struct {
    VkInstance instance;
    VkPhysicalDevice phys;
    VkDevice device;
    uint32_t qfam;
    VkQueue queue;
    VkCommandPool cmdpool;
    VkPhysicalDeviceProperties props;
    VkPhysicalDeviceMemoryProperties memprops;
    bool has_tess;
    bool tess_split_pass;
    uint32_t tier;
    VkFormat depth_format;
    VkSampleCountFlagBits msaa;
    VkDescriptorPool dpool;
    VkSampler sampler_repeat;
    VkSampler sampler_clamp;
    VkSampler sampler_shadow;
    VkPipelineCache pcache;
} VkCore;

extern VkResult (*vkc_instance_hook)(const VkInstanceCreateInfo *ci, VkInstance *out);
extern VkResult (*vkc_device_hook)(VkPhysicalDevice phys, const VkDeviceCreateInfo *ci, VkDevice *out);

int vkc_create_instance(VkCore *c, const char **ext, uint32_t next);
int vkc_pick_device(VkCore *c, VkPhysicalDevice forced);
int vkc_create_device(VkCore *c, const char **ext, uint32_t next);
int vkc_finish_setup(VkCore *c);
void vkc_core_destroy(VkCore *c);

typedef struct {
    VkCore *core;
    uint32_t width, height, layers;
    VkFormat color_format;
    VkRenderPass rp;
    uint32_t target_count;
    VkImage target_images[8];
    VkImageView target_views[8];
    VkFramebuffer fbs[8];
} RenderTargets;

int vkc_targets_create(RenderTargets *rt, VkCore *c, uint32_t w, uint32_t h, uint32_t layers,
                       VkFormat fmt, VkImage *images, uint32_t count, VkImageLayout final_layout);
void vkc_targets_destroy(RenderTargets *rt);

typedef struct {
    VkCore *core;
    VkRenderPass rp;
    VkFramebuffer fb;
    uint32_t width, height;
} Pass;

int vkc_image_create(VkCore *c, uint32_t w, uint32_t h, uint32_t layers, uint32_t mips,
                     VkFormat fmt, VkSampleCountFlagBits samples, VkImageUsageFlags usage,
                     bool transient, VImg *out);
int vkc_pass_scene(Pass *p, VkCore *c, VkFormat fmt, VkImageView msaa_color, VkImageView msaa_depth,
                   VkImageView resolve, uint32_t w, uint32_t h, uint32_t view_mask);
int vkc_pass_scene_pre(Pass *p, VkCore *c, VkFormat fmt, VkImageView msaa_color, VkImageView msaa_depth,
                       uint32_t w, uint32_t h, uint32_t view_mask);
int vkc_pass_scene_main(Pass *p, VkCore *c, VkFormat fmt, VkImageView msaa_color, VkImageView msaa_depth,
                        VkImageView resolve, uint32_t w, uint32_t h, uint32_t view_mask);
int vkc_pass_color(Pass *p, VkCore *c, VkFormat fmt, VkImageView target, uint32_t w, uint32_t h,
                   uint32_t view_mask, bool load_existing);
int vkc_pass_depth(Pass *p, VkCore *c, VkFormat fmt, VkImageView target, uint32_t w, uint32_t h);
int vkc_pipe_depth(VkCore *c, VkRenderPass rp, VkPipelineLayout layout, const char *vs_asset,
                   const VkVertexInputBindingDescription *vb, uint32_t nvb,
                   const VkVertexInputAttributeDescription *va, uint32_t nva, VkPipeline *out);
void vkc_pass_begin(VkCommandBuffer cmd, const Pass *p, const VkClearValue *clears, uint32_t nclears);
void vkc_pass_destroy(Pass *p);
const VkSpecializationInfo *vkc_tier_spec(VkCore *c);

int render_init(VkCore *c, uint32_t w, uint32_t h, uint32_t layers);
VkRenderPass render_scene_rp(void);
VkRenderPass render_terrain_rp(void);
VkRenderPass render_shadow_rp(void);
VkImageView render_shadowmap_view(void);
void render_fill_cascades(v3 campos, v3 sundir, FrameUBO *ubo);
int render_post_init(VkCore *c, VkRenderPass final_rp);
int render_resize(uint32_t w, uint32_t h);
void render_record(VkCommandBuffer cmd, uint32_t slot, VkFramebuffer final_fb, uint32_t fw, uint32_t fh);
void render_destroy(void);

typedef struct {
    VkCore *core;
    VkSurfaceKHR surface;
    VkSwapchainKHR swapchain;
    RenderTargets rt;
    VkCommandBuffer cmd[VISTA_FRAMES];
    VkFence fence[VISTA_FRAMES];
    VkSemaphore sem_acquire[VISTA_FRAMES];
    VkSemaphore sem_release[8];
    uint32_t slot;
    bool transfer_src;
} FlatChain;

int vkc_flatchain_create(FlatChain *fc, VkCore *c, VkSurfaceKHR surface, uint32_t w, uint32_t h);
void vkc_flatchain_destroy(FlatChain *fc);
int vkc_flatchain_frame(FlatChain *fc, const FrameUBO *ubo);
int vkc_flatchain_screenshot(FlatChain *fc, const FrameUBO *ubo, const char *path);

typedef struct {
    VkCore *core;
    RenderTargets rt;
    VImg color;
    VkCommandBuffer cmd[VISTA_FRAMES];
    VkFence fence[VISTA_FRAMES];
    uint32_t slot;
} OffChain;

int vkc_offchain_create(OffChain *oc, VkCore *c, uint32_t w, uint32_t h);
void vkc_offchain_destroy(OffChain *oc);
int vkc_offchain_frame(OffChain *oc, const FrameUBO *ubo);
int vkc_offchain_screenshot(OffChain *oc, const FrameUBO *ubo, const char *path);

int vkc_buffer(VkCore *c, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props, VBuf *out);
void vkc_buffer_destroy(VkCore *c, VBuf *b);
int vkc_texture_rgba(VkCore *c, const uint8_t *px, uint32_t w, uint32_t h, bool srgb, bool genmips, VImg *out);
int vkc_texture_r16(VkCore *c, const uint16_t *px, uint32_t w, uint32_t h, VImg *out);
void vkc_image_destroy(VkCore *c, VImg *i);
VkShaderModule vkc_shader(VkCore *c, const char *asset_name);
VkCommandBuffer vkc_begin_once(VkCore *c);
void vkc_end_once(VkCore *c, VkCommandBuffer cmd);

typedef struct {
    VkCore *core;
    VkDescriptorSetLayout set0_layout;
    VkDescriptorSetLayout texset_layout;
    VkPipelineLayout pipe_layout;
    VkDescriptorSet set0[VISTA_FRAMES];
    VkDescriptorSet material_set;
    VBuf ubo[VISTA_FRAMES];
    bool multiview;
} Scene;

int scene_alloc_texset(VkCore *c, const VkDescriptorImageInfo *imgs, uint32_t n, VkDescriptorSet *out);
void scene_write_texset(VkCore *c, VkDescriptorSet set, const VkDescriptorImageInfo *imgs, uint32_t n);
void scene_write_luts(VkCore *c, VkImageView t, VkImageView ms, VkImageView sky);

void atmos_update(v3 sundir, float cam_y, FrameUBO *out);

extern Scene g_scene;

int scene_init(VkCore *c, VkRenderPass rp, bool multiview);
void scene_update_ubo(uint32_t slot, const FrameUBO *ubo);
void scene_record(VkCommandBuffer cmd, uint32_t slot, uint32_t w, uint32_t h);
void scene_record_terrain(VkCommandBuffer cmd, uint32_t slot, uint32_t w, uint32_t h);
void scene_record_rest(VkCommandBuffer cmd, uint32_t slot, uint32_t w, uint32_t h);
void scene_record_shadow(VkCommandBuffer cmd, uint32_t slot, uint32_t cascade, uint32_t res);
void scene_destroy(void);

int terrain_init(VkCore *c, VkRenderPass rp, bool multiview, const Scene *scene);
void terrain_record(VkCommandBuffer cmd, uint32_t slot, const FrameUBO *ubo);
void terrain_record_shadow(VkCommandBuffer cmd, const FrameUBO *ubo, uint32_t cascade);
void terrain_destroy(void);
float terrain_height_at(float x, float z);
float terrain_height_smooth_at(float x, float z);
const uint16_t *terrain_heightmap(void);
VImg *terrain_heightmap_tex(void);

int sky_init(VkCore *c, VkRenderPass rp, bool multiview, const Scene *scene);
void sky_record(VkCommandBuffer cmd, uint32_t slot);
void sky_destroy(void);

int veg_init(VkCore *c, VkRenderPass rp, bool multiview, const Scene *scene);
void veg_update(v3 campos, uint32_t slot);
void veg_record(VkCommandBuffer cmd, uint32_t slot);
void veg_record_shadow(VkCommandBuffer cmd, uint32_t slot, const FrameUBO *ubo, uint32_t cascade);
void veg_destroy(void);
float veg_min_tree_dist(v3 p);

#define WATER_LEVEL 58.0f

int water_init(VkCore *c, VkRenderPass rp, bool multiview, const Scene *scene);
void water_record(VkCommandBuffer cmd, uint32_t slot);
void water_destroy(void);

VImg *terrain_lightmap_tex(void);
VImg *terrain_horizon_tex(uint32_t i);

float pg_clamp01(float v);
float pg_smoothstep(float a, float b, float x);
float pg_vnoise2(float x, float z, uint32_t seed);
float pg_fbm2(float x, float z, int oct, uint32_t seed);
float pg_ridged2(float x, float z, int oct, uint32_t seed);
float pg_forest_density(float wx, float wz);
float pg_forest_mask(float wx, float wz);
float pg_terrain_macro(float x, float z);

typedef struct {
    v3 pos;
    v3 vel;
    float yaw, pitch;
    bool grounded;
    double time;
} GameState;

extern GameState g_game;

void game_init(GameState *g);
void game_update(GameState *g, const Input *in, float dt);
void game_flat_ubo(const GameState *g, float aspect, FrameUBO *out);
void game_fill_light(const GameState *g, FrameUBO *out);
v3 game_sun_dir(const GameState *g);

typedef struct {
    VImg grass_color, grass_normal;
    VImg rock_color, rock_normal;
    VImg dirt_color, dirt_normal;
    VImg snow_color, snow_normal;
} Textures;

extern Textures g_tex;

int assets_load_textures(VkCore *c);
void assets_destroy_textures(VkCore *c);

uint8_t *plat_load_asset(const char *name, int *out_size);
void plat_log(const char *fmt, ...);
double plat_time(void);

#ifdef VISTA_VR
typedef struct XrCtx XrCtx;
int xr_create(VkCore *c);
int xr_running(void);
int xr_frame(GameState *g, Input *in, float *out_dt);
void xr_shutdown(void);
#endif

#endif
