#include "vista.h"
#include <stdio.h>
#include <string.h>

#ifdef __ANDROID__
#define GRASS_COUNT 32000
#define ROCK_COUNT 400
#define TREE_COUNT 3600
#define GRASS_R 68.0f
#define ROCK_R 260.0f
#else
#define GRASS_COUNT 240000
#define ROCK_COUNT 2000
#define TREE_COUNT 28000
#define GRASS_R 130.0f
#define ROCK_R 420.0f
#endif

#define GRASS_STEP (GRASS_COUNT/64)
#define ROCK_STEP (ROCK_COUNT/64)
#define ROCK_VERTS 240
#define GRASS_VERTS 12
#define TREE_RS 20
#define TREE_MAXV 256
#define TREE_MAXI 512
#define TREE_TOP_Y 6.6f
#define VEG_PC_STAGES (VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT | \
                       VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)

typedef struct {
    float p[4];
    float q[4];
} Inst;

static struct {
    VkCore *core;
    const Scene *scene;
    VkPipeline grass_pipe;
    VkPipeline rock_pipe;
    VkPipeline tree_pipe;
    VkPipeline rock_shadow_pipe;
    VkPipeline tree_shadow_pipe;
    VBuf grass_buf[VISTA_FRAMES];
    VBuf rock_buf[VISTA_FRAMES];
    VBuf rock_vb;
    VBuf tree_vb;
    VBuf tree_ib;
    VBuf tree_instbuf;
    Inst grass[GRASS_COUNT];
    Inst rocks[ROCK_COUNT];
    Inst trees[TREE_COUNT];
    uint32_t gcur, rcur, rng;
    uint64_t gwritten, rwritten;
    uint64_t gsynced[VISTA_FRAMES], rsynced[VISTA_FRAMES];
    uint32_t tree_count;
    uint32_t tree_icount;
    bool seeded;
    float nr_x[256], nr_z[256], nr_rad[256];
    int nr_count;
} s_veg;

static float rock_mesh[ROCK_VERTS*6];
static float tree_mesh[TREE_MAXV*9];
static uint16_t tree_idx[TREE_MAXI];
static uint32_t tree_vcount;

static uint32_t rnd(void){
    s_veg.rng = s_veg.rng*1664525u + 1013904223u;
    return s_veg.rng;
}

static float rndf(void){
    return (float)(rnd() >> 8)*(1.0f/16777216.0f);
}

static float slope_at(float x, float z){
    float e = 1.5f;
    float hx = terrain_height_at(x+e, z) - terrain_height_at(x-e, z);
    float hz = terrain_height_at(x, z+e) - terrain_height_at(x, z-e);
    return sqrtf(hx*hx + hz*hz)/(2.0f*e);
}

static uint32_t vhash2(int32_t x, int32_t z, uint32_t seed){
    uint32_t h = seed + (uint32_t)x*0x27d4eb2fu + (uint32_t)z*0x9e3779b9u;
    h ^= h >> 15; h *= 0x85ebca6bu;
    h ^= h >> 13; h *= 0xc2b2ae35u;
    h ^= h >> 16;
    return h;
}

static float vlattice(int x, int z, uint32_t seed){
    return (float)(vhash2(x, z, seed) & 0xFFFFFFu)*(1.0f/16777215.0f);
}

static float vnoise2f(float x, float z, uint32_t seed){
    float fx = floorf(x), fz = floorf(z);
    int xi = (int)fx, zi = (int)fz;
    float tx = x - fx, tz = z - fz;
    float sx = tx*tx*(3.0f - 2.0f*tx);
    float sz = tz*tz*(3.0f - 2.0f*tz);
    float a = vlattice(xi, zi, seed);
    float b = vlattice(xi+1, zi, seed);
    float c = vlattice(xi, zi+1, seed);
    float d = vlattice(xi+1, zi+1, seed);
    float m0 = a + (b-a)*sx;
    float m1 = c + (d-c)*sx;
    return m0 + (m1-m0)*sz;
}

static float forest_fbm(float x, float z, int oct, uint32_t seed){
    float v = 0.0f, amp = 0.5f, sum = 0.0f;
    for(int i = 0; i < oct; i++){
        v += amp*vnoise2f(x, z, seed + (uint32_t)i*257u);
        sum += amp;
        float xr = x*2.02f + 3.1f;
        float zr = z*2.02f + 7.7f;
        x = xr; z = zr;
        amp *= 0.5f;
    }
    return v/sum;
}

static float forest_mask(float wx, float wz){
    float regional = forest_fbm(wx*0.0009f, wz*0.0009f, 2, 0xF0EE5Du);
    float clumps = forest_fbm(wx*0.0035f + 41.0f, wz*0.0035f + 17.0f, 3, 0x1357Bu);
    float density = regional*0.6f + clumps*0.4f;
    return density;
}

static float clamp01(float v){
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static float terrain_hashn(float px, float pz){
    float v = sinf(px*127.1f + pz*311.7f)*43758.5453f;
    return v - floorf(v);
}

static float terrain_vnoise(float x, float z){
    float ix = floorf(x), iz = floorf(z);
    float fx = x - ix, fz = z - iz;
    float sx = fx*fx*(3.0f - 2.0f*fx);
    float sz = fz*fz*(3.0f - 2.0f*fz);
    float a = terrain_hashn(ix, iz);
    float b = terrain_hashn(ix + 1.0f, iz);
    float c = terrain_hashn(ix, iz + 1.0f);
    float d = terrain_hashn(ix + 1.0f, iz + 1.0f);
    float m0 = a + (b - a)*sx;
    float m1 = c + (d - c)*sx;
    return m0 + (m1 - m0)*sz;
}

static float terrain_macro_at(float x, float z){
    float px = x*0.0025f + 19.0f, pz = z*0.0025f + 19.0f;
    float v = 0.0f, amp = 0.5f;
    for(int i = 0; i < 3; i++){
        v += amp*terrain_vnoise(px, pz);
        float nx = px*2.03f + 11.3f;
        float nz = pz*2.03f + 7.9f;
        px = nx; pz = nz;
        amp *= 0.5f;
    }
    return v;
}

static bool is_shoreline(float h){
    return h > WATER_LEVEL - 0.5f && h < WATER_LEVEL + 14.0f;
}

static bool is_scree(float x, float z, float slope){
    if(slope > 0.55f) return false;
    float e = 10.0f;
    float maxup = 0.0f;
    for(int k = 0; k < 4; k++){
        float ang = (float)k*1.5707963f;
        float sx = x + e*cosf(ang);
        float sz = z + e*sinf(ang);
        float sslope = slope_at(sx, sz);
        if(sslope > maxup) maxup = sslope;
    }
    return maxup > 0.7f;
}

static void rebuild_nearby_rocks(v3 cp){
    float cut2 = (GRASS_R + 12.0f)*(GRASS_R + 12.0f);
    int n = 0;
    for(uint32_t i = 0; i < ROCK_COUNT && n < 256; i++){
        Inst *r = &s_veg.rocks[i];
        if(r->p[3] <= 0.0f) continue;
        float dx = r->p[0] - cp.x, dz = r->p[2] - cp.z;
        if(dx*dx + dz*dz > cut2) continue;
        s_veg.nr_x[n] = r->p[0];
        s_veg.nr_z[n] = r->p[2];
        s_veg.nr_rad[n] = r->p[3]*1.35f + 1.1f;
        n++;
    }
    s_veg.nr_count = n;
}

static bool near_rock(float x, float z){
    for(int i = 0; i < s_veg.nr_count; i++){
        float dx = x - s_veg.nr_x[i], dz = z - s_veg.nr_z[i];
        float rr = s_veg.nr_rad[i];
        if(dx*dx + dz*dz < rr*rr) return true;
    }
    return false;
}

static void scatter_grass(v3 cp, Inst *g){
    for(int t = 0; t < 6; t++){
        float r = GRASS_R*sqrtf(rndf());
        float a = rndf()*6.2831853f;
        float x = cp.x + r*cosf(a);
        float z = cp.z + r*sinf(a);
        float h = terrain_height_at(x, z);
        if(h < WATER_LEVEL + 0.4f || h > TERRAIN_HEIGHT*0.55f) continue;
        if(slope_at(x, z) > 0.5f) continue;
        if(near_rock(x, z)) continue;
        g->p[0] = x; g->p[1] = h - 0.03f; g->p[2] = z;
        g->p[3] = 0.5f + 0.7f*rndf();
        g->q[0] = rndf()*3.1415927f;
        g->q[1] = rndf()*6.2831853f;
        float macro = clamp01(terrain_macro_at(x, z)/0.875f);
        float dryness = clamp01(1.0f - macro + (rndf() - 0.5f)*0.16f);
        g->q[2] = dryness;
        g->q[3] = GRASS_R;
        return;
    }
    g->p[0] = cp.x; g->p[1] = 0.0f; g->p[2] = cp.z; g->p[3] = 0.0f;
}

static void scatter_rock(v3 cp, Inst *g){
    bool have_best = false;
    float bx = 0.0f, bz = 0.0f, bh = 0.0f;
    for(int t = 0; t < 28; t++){
        float r = ROCK_R*sqrtf(rndf());
        float a = rndf()*6.2831853f;
        float x = cp.x + r*cosf(a);
        float z = cp.z + r*sinf(a);
        float h = terrain_height_at(x, z);
        if(h < 0.5f) continue;
        float sl = slope_at(x, z);
        if(sl > 0.9f) continue;
        bool pref = is_shoreline(h) || is_scree(x, z, sl);
        if(!pref){
            if(have_best) continue;
            bx = x; bz = z; bh = h;
            have_best = true;
            continue;
        }
        float sc = 0.4f + 2.2f*rndf()*rndf();
        g->p[0] = x; g->p[1] = h - sc*0.25f; g->p[2] = z;
        g->p[3] = sc;
        g->q[0] = rndf()*6.2831853f;
        g->q[1] = rndf();
        g->q[2] = rndf();
        g->q[3] = ROCK_R;
        return;
    }
    if(have_best && rndf() < 0.12f){
        float sc = 0.4f + 2.2f*rndf()*rndf();
        g->p[0] = bx; g->p[1] = bh - sc*0.25f; g->p[2] = bz;
        g->p[3] = sc;
        g->q[0] = rndf()*6.2831853f;
        g->q[1] = rndf();
        g->q[2] = rndf();
        g->q[3] = ROCK_R;
        return;
    }
    g->p[0] = cp.x; g->p[1] = 0.0f; g->p[2] = cp.z; g->p[3] = 0.0f;
}

static void rock_emit(int *o, v3 a, v3 b, v3 c){
    v3 v[3] = { a, b, c };
    for(int k = 0; k < 3; k++){
        v3 n = v3norm(v[k]);
        float *d = rock_mesh + *o;
        d[0] = v[k].x; d[1] = v[k].y; d[2] = v[k].z;
        d[3] = n.x; d[4] = n.y; d[5] = n.z;
        *o += 6;
    }
}

static void rock_gen(void){
    static const float A = 0.5257311f, B = 0.8506508f;
    static const float V[12][3] = {
        {-A,B,0},{A,B,0},{-A,-B,0},{A,-B,0},
        {0,-A,B},{0,A,B},{0,-A,-B},{0,A,-B},
        {B,0,-A},{B,0,A},{-B,0,-A},{-B,0,A}
    };
    static const int F[20][3] = {
        {0,11,5},{0,5,1},{0,1,7},{0,7,10},{0,10,11},
        {1,5,9},{5,11,4},{11,10,2},{10,7,6},{7,1,8},
        {3,9,4},{3,4,2},{3,2,6},{3,6,8},{3,8,9},
        {4,9,5},{2,4,11},{6,2,10},{8,6,7},{9,8,1}
    };
    int o = 0;
    for(int f = 0; f < 20; f++){
        v3 a = (v3){ V[F[f][0]][0], V[F[f][0]][1], V[F[f][0]][2] };
        v3 b = (v3){ V[F[f][1]][0], V[F[f][1]][1], V[F[f][1]][2] };
        v3 c = (v3){ V[F[f][2]][0], V[F[f][2]][1], V[F[f][2]][2] };
        v3 m0 = v3norm(v3add(a, b));
        v3 m1 = v3norm(v3add(b, c));
        v3 m2 = v3norm(v3add(c, a));
        rock_emit(&o, a, m0, m2);
        rock_emit(&o, m0, b, m1);
        rock_emit(&o, m2, m1, c);
        rock_emit(&o, m0, m1, m2);
    }
}

static void tv_push(float x, float y, float z, float nx, float ny, float nz, float r, float g, float b){
    float *d = tree_mesh + tree_vcount*9;
    d[0]=x; d[1]=y; d[2]=z; d[3]=nx; d[4]=ny; d[5]=nz; d[6]=r; d[7]=g; d[8]=b;
    tree_vcount++;
}

static void tv_tri(uint32_t a, uint32_t b, uint32_t c){
    tree_idx[s_veg.tree_icount++] = (uint16_t)a;
    tree_idx[s_veg.tree_icount++] = (uint16_t)b;
    tree_idx[s_veg.tree_icount++] = (uint16_t)c;
}

static void build_trunk(void){
    const int n = 6;
    const float rbot = 0.16f, rtop = 0.10f, y0 = 0.0f, y1 = 2.0f;
    uint32_t base = tree_vcount;
    for(int i = 0; i < n; i++){
        float ang = (float)i/(float)n*6.2831853f;
        float cx = cosf(ang), cz = sinf(ang);
        tv_push(cx*rbot, y0, cz*rbot, cx, 0.0f, cz, 0.25f, 0.16f, 0.10f);
    }
    for(int i = 0; i < n; i++){
        float ang = (float)i/(float)n*6.2831853f;
        float cx = cosf(ang), cz = sinf(ang);
        tv_push(cx*rtop, y1, cz*rtop, cx, 0.0f, cz, 0.25f, 0.16f, 0.10f);
    }
    for(int i = 0; i < n; i++){
        int i2 = (i+1)%n;
        tv_tri(base+i, base+n+i, base+i2);
        tv_tri(base+i2, base+n+i, base+n+i2);
    }
}

static void build_cone(float basey, float apexy, float baser, float r, float g, float b){
    uint32_t ringbase = tree_vcount;
    float dy = apexy - basey;
    float slopelen = sqrtf(baser*baser + dy*dy);
    float ny = baser/slopelen;
    float nxz = dy/slopelen;
    for(int i = 0; i < TREE_RS; i++){
        float ang = (float)i/(float)TREE_RS*6.2831853f;
        float cx = cosf(ang), cz = sinf(ang);
        float rj = baser*(0.80f + 0.22f*sinf(ang*9.0f + baser*17.0f) + 0.10f*sinf(ang*23.0f + baser*5.0f));
        float yj = basey + baser*0.18f*sinf(ang*13.0f + baser*7.0f);
        tv_push(cx*rj, yj, cz*rj, cx*nxz, ny, cz*nxz, r, g, b);
    }
    uint32_t apexidx = tree_vcount;
    tv_push(0.0f, apexy, 0.0f, 0.0f, 1.0f, 0.0f, r, g, b);
    for(int i = 0; i < TREE_RS; i++){
        int i2 = (i+1)%TREE_RS;
        tv_tri(ringbase+i, apexidx, ringbase+i2);
    }
    uint32_t capcenter = tree_vcount;
    tv_push(0.0f, basey, 0.0f, 0.0f, -1.0f, 0.0f, r, g, b);
    uint32_t capring = tree_vcount;
    for(int i = 0; i < TREE_RS; i++){
        float ang = (float)i/(float)TREE_RS*6.2831853f;
        float cx = cosf(ang), cz = sinf(ang);
        tv_push(cx*baser, basey, cz*baser, 0.0f, -1.0f, 0.0f, r, g, b);
    }
    for(int i = 0; i < TREE_RS; i++){
        int i2 = (i+1)%TREE_RS;
        tv_tri(capcenter, capring+i2, capring+i);
    }
}

static void tree_gen(void){
    tree_vcount = 0;
    s_veg.tree_icount = 0;
    build_trunk();
    build_cone(0.9f, 3.6f, 1.35f, 0.026f, 0.085f, 0.028f);
    build_cone(2.6f, 5.4f, 1.00f, 0.060f, 0.155f, 0.058f);
    build_cone(4.4f, TREE_TOP_Y, 0.65f, 0.040f, 0.115f, 0.042f);
}

static void scatter_trees(void){
    uint32_t placed = 0;
    uint32_t max_attempts = TREE_COUNT*260;
    float half = TERRAIN_SCALE*0.5f;
    for(uint32_t a = 0; a < max_attempts && placed < TREE_COUNT; a++){
        float x = (rndf()*2.0f - 1.0f)*half;
        float z = (rndf()*2.0f - 1.0f)*half;
        float h = terrain_height_at(x, z);
        float hn = h/TERRAIN_HEIGHT;
        if(hn < 0.09f || hn > 0.56f) continue;
        if(h < WATER_LEVEL + 1.5f) continue;
        if(slope_at(x, z) > 0.42f) continue;
        float dens = clamp01(forest_mask(x, z));
        if(dens < 0.40f) continue;
        float edge = clamp01((dens - 0.40f)/0.12f);
        float keep = edge*edge*(3.0f - 2.0f*edge)*0.85f + 0.15f;
        if(rndf() > keep) continue;
        float hs = terrain_height_smooth_at(x, z);
        float hbase = h - clamp01((h - hs) * 0.4f) * 1.5f;
        Inst *g = &s_veg.trees[placed++];
        g->p[0] = x; g->p[1] = hbase - 0.25f; g->p[2] = z; g->p[3] = 0.7f + 0.8f*rndf();
        g->q[0] = rndf()*6.2831853f;
        g->q[1] = rndf();
        g->q[2] = 0.0f;
        g->q[3] = 0.0f;
    }
    s_veg.tree_count = placed;
}

static int veg_pipe(VkCore *c, VkRenderPass rp, bool multiview, VkPipelineLayout layout,
                    const char *vsn, const char *fsn,
                    const VkVertexInputBindingDescription *vb, uint32_t nvb,
                    const VkVertexInputAttributeDescription *va, uint32_t nva,
                    VkBool32 a2c, VkBool32 blend, VkPipeline *out){
    char nm[96];
    snprintf(nm, sizeof nm, "shaders/%s%s.spv", vsn, multiview ? ".mv" : "");
    VkShaderModule vs = vkc_shader(c, nm);
    snprintf(nm, sizeof nm, "shaders/%s%s.spv", fsn, multiview ? ".mv" : "");
    VkShaderModule fs = vkc_shader(c, nm);
    if(vs == VK_NULL_HANDLE || fs == VK_NULL_HANDLE){
        if(vs) vkDestroyShaderModule(c->device, vs, NULL);
        if(fs) vkDestroyShaderModule(c->device, fs, NULL);
        return -1;
    }
    VkPipelineShaderStageCreateInfo stages[2] = {
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vs, .pName = "main",
          .pSpecializationInfo = vkc_tier_spec(c) },
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fs, .pName = "main",
          .pSpecializationInfo = vkc_tier_spec(c) }
    };
    VkPipelineVertexInputStateCreateInfo vi = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = nvb, .pVertexBindingDescriptions = vb,
        .vertexAttributeDescriptionCount = nva, .pVertexAttributeDescriptions = va
    };
    VkPipelineInputAssemblyStateCreateInfo ia = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
    };
    VkPipelineViewportStateCreateInfo vp = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1, .scissorCount = 1
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
        .rasterizationSamples = c->msaa,
        .alphaToCoverageEnable = a2c
    };
    VkPipelineDepthStencilStateCreateInfo ds = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = blend ? VK_FALSE : VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL
    };
    VkPipelineColorBlendAttachmentState att = {
        .blendEnable = blend,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
    };
    VkPipelineColorBlendStateCreateInfo cb = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &att
    };
    VkDynamicState dyn[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dy = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2, .pDynamicStates = dyn
    };
    VkGraphicsPipelineCreateInfo pi = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2, .pStages = stages,
        .pVertexInputState = &vi,
        .pInputAssemblyState = &ia,
        .pViewportState = &vp,
        .pRasterizationState = &rs,
        .pMultisampleState = &ms,
        .pDepthStencilState = &ds,
        .pColorBlendState = &cb,
        .pDynamicState = &dy,
        .layout = layout,
        .renderPass = rp
    };
    VkResult r = vkCreateGraphicsPipelines(c->device, c->pcache, 1, &pi, NULL, out);
    vkDestroyShaderModule(c->device, vs, NULL);
    vkDestroyShaderModule(c->device, fs, NULL);
    return r == VK_SUCCESS ? 0 : -1;
}

static int upload_static(VkCore *c, const void *data, VkDeviceSize sz, VkBufferUsageFlags usage, VBuf *out){
    VBuf st;
    if(vkc_buffer(c, sz, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &st))
        return -1;
    if(!st.map){ vkc_buffer_destroy(c, &st); return -1; }
    memcpy(st.map, data, sz);
    if(vkc_buffer(c, sz, VK_BUFFER_USAGE_TRANSFER_DST_BIT | usage,
                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, out)){
        vkc_buffer_destroy(c, &st);
        return -1;
    }
    VkCommandBuffer cmd = vkc_begin_once(c);
    if(cmd == VK_NULL_HANDLE){ vkc_buffer_destroy(c, &st); return -1; }
    VkBufferCopy region = { 0, 0, sz };
    vkCmdCopyBuffer(cmd, st.buf, out->buf, 1, &region);
    vkc_end_once(c, cmd);
    vkc_buffer_destroy(c, &st);
    return 0;
}

int veg_init(VkCore *c, VkRenderPass rp, bool multiview, const Scene *scene){
    s_veg.core = c;
    s_veg.scene = scene;
    s_veg.rng = 0x1337c0deu;
    s_veg.gcur = 0;
    s_veg.rcur = 0;
    s_veg.gwritten = 0;
    s_veg.rwritten = 0;
    memset(s_veg.gsynced, 0, sizeof s_veg.gsynced);
    memset(s_veg.rsynced, 0, sizeof s_veg.rsynced);
    s_veg.seeded = false;
    rock_gen();
    if(upload_static(c, rock_mesh, sizeof rock_mesh, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &s_veg.rock_vb))
        return -1;
    tree_gen();
    if(upload_static(c, tree_mesh, sizeof(float)*9*tree_vcount, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &s_veg.tree_vb))
        return -1;
    if(upload_static(c, tree_idx, sizeof(uint16_t)*s_veg.tree_icount, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, &s_veg.tree_ib))
        return -1;
    scatter_trees();
    if(upload_static(c, s_veg.trees, sizeof s_veg.trees, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &s_veg.tree_instbuf))
        return -1;
    for(uint32_t i = 0; i < VISTA_FRAMES; i++){
        if(vkc_buffer(c, sizeof s_veg.grass, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      &s_veg.grass_buf[i]))
            return -1;
        if(!s_veg.grass_buf[i].map) return -1;
        if(vkc_buffer(c, sizeof s_veg.rocks, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      &s_veg.rock_buf[i]))
            return -1;
        if(!s_veg.rock_buf[i].map) return -1;
    }
    VkVertexInputBindingDescription gvb[1] = {
        { .binding = 0, .stride = sizeof(Inst), .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE }
    };
    VkVertexInputAttributeDescription gva[2] = {
        { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = 0 },
        { .location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = 16 }
    };
    if(veg_pipe(c, rp, multiview, scene->pipe_layout, "grass.vert", "grass.frag",
                gvb, 1, gva, 2, VK_TRUE, VK_FALSE, &s_veg.grass_pipe))
        return -1;
    VkVertexInputBindingDescription rvb[2] = {
        { .binding = 0, .stride = 6*sizeof(float), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX },
        { .binding = 1, .stride = sizeof(Inst), .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE }
    };
    VkVertexInputAttributeDescription rva[4] = {
        { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = 0 },
        { .location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = 12 },
        { .location = 2, .binding = 1, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = 0 },
        { .location = 3, .binding = 1, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = 16 }
    };
    if(veg_pipe(c, rp, multiview, scene->pipe_layout, "rock.vert", "rock.frag",
                rvb, 2, rva, 4, VK_FALSE, VK_FALSE, &s_veg.rock_pipe))
        return -1;
    if(veg_pipe(c, rp, multiview, scene->pipe_layout, "rock.vert", "rock.frag",
                rvb, 2, rva, 4, VK_FALSE, VK_TRUE, &s_veg.rock_shadow_pipe))
        return -1;
    VkVertexInputBindingDescription tvb[2] = {
        { .binding = 0, .stride = 9*sizeof(float), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX },
        { .binding = 1, .stride = sizeof(Inst), .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE }
    };
    VkVertexInputAttributeDescription tva[5] = {
        { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = 0 },
        { .location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = 12 },
        { .location = 2, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = 24 },
        { .location = 3, .binding = 1, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = 0 },
        { .location = 4, .binding = 1, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = 16 }
    };
    if(veg_pipe(c, rp, multiview, scene->pipe_layout, "tree.vert", "tree.frag",
                tvb, 2, tva, 5, VK_FALSE, VK_FALSE, &s_veg.tree_pipe))
        return -1;
    if(veg_pipe(c, rp, multiview, scene->pipe_layout, "tree.vert", "tree.frag",
                tvb, 2, tva, 5, VK_FALSE, VK_TRUE, &s_veg.tree_shadow_pipe))
        return -1;
    return 0;
}

static void sync_slot(VBuf *dst, const Inst *src, uint32_t count, uint64_t *synced, uint64_t written){
    uint64_t pending = written - *synced;
    *synced = written;
    if(pending == 0) return;
    if(pending >= count){
        memcpy(dst->map, src, sizeof(Inst)*count);
        return;
    }
    uint32_t start = (uint32_t)((written - pending)%count);
    uint32_t n1 = count - start;
    if((uint64_t)n1 > pending) n1 = (uint32_t)pending;
    memcpy((Inst*)dst->map + start, src + start, sizeof(Inst)*n1);
    uint32_t n2 = (uint32_t)(pending - n1);
    if(n2) memcpy(dst->map, src, sizeof(Inst)*n2);
}

void veg_update(v3 campos, uint32_t slot){
    if(slot >= VISTA_FRAMES || !s_veg.grass_buf[slot].map || !s_veg.rock_buf[slot].map)
        return;
    uint32_t gn = s_veg.seeded ? GRASS_STEP : GRASS_COUNT;
    uint32_t rn = s_veg.seeded ? ROCK_STEP : ROCK_COUNT;
    float gr2 = GRASS_R*GRASS_R;
    float rr2 = ROCK_R*ROCK_R;
    for(uint32_t i = 0; i < rn; i++){
        uint32_t k = (s_veg.rcur + i)%ROCK_COUNT;
        Inst *g = &s_veg.rocks[k];
        float dx = g->p[0] - campos.x;
        float dz = g->p[2] - campos.z;
        if(!s_veg.seeded || g->p[3] == 0.0f || dx*dx + dz*dz > rr2)
            scatter_rock(campos, g);
    }
    s_veg.rcur = (s_veg.rcur + rn)%ROCK_COUNT;
    rebuild_nearby_rocks(campos);
    for(uint32_t i = 0; i < gn; i++){
        uint32_t k = (s_veg.gcur + i)%GRASS_COUNT;
        Inst *g = &s_veg.grass[k];
        float dx = g->p[0] - campos.x;
        float dz = g->p[2] - campos.z;
        if(!s_veg.seeded || g->p[3] == 0.0f || dx*dx + dz*dz > gr2)
            scatter_grass(campos, g);
    }
    s_veg.gcur = (s_veg.gcur + gn)%GRASS_COUNT;
    s_veg.gwritten += gn;
    s_veg.rwritten += rn;
    s_veg.seeded = true;
    sync_slot(&s_veg.grass_buf[slot], s_veg.grass, GRASS_COUNT, &s_veg.gsynced[slot], s_veg.gwritten);
    sync_slot(&s_veg.rock_buf[slot], s_veg.rocks, ROCK_COUNT, &s_veg.rsynced[slot], s_veg.rwritten);
}

void veg_record(VkCommandBuffer cmd, uint32_t slot){
    if(slot >= VISTA_FRAMES || !s_veg.scene || s_veg.grass_pipe == VK_NULL_HANDLE)
        return;
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s_veg.scene->pipe_layout,
                            0, 1, &s_veg.scene->set0[slot], 0, NULL);
    VkDeviceSize zero = 0;
    float pcShadow[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
    float pcNormal[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    VkBuffer tbufs[2] = { s_veg.tree_vb.buf, s_veg.tree_instbuf.buf };
    VkDeviceSize toffs[2] = { 0, 0 };
    VkBuffer rbufs[2] = { s_veg.rock_vb.buf, s_veg.rock_buf[slot].buf };
    VkDeviceSize roffs[2] = { 0, 0 };
    if(s_veg.tree_shadow_pipe != VK_NULL_HANDLE && s_veg.tree_count > 0){
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s_veg.tree_shadow_pipe);
        vkCmdPushConstants(cmd, s_veg.scene->pipe_layout, VEG_PC_STAGES, 0, sizeof pcShadow, pcShadow);
        vkCmdBindVertexBuffers(cmd, 0, 2, tbufs, toffs);
        vkCmdBindIndexBuffer(cmd, s_veg.tree_ib.buf, 0, VK_INDEX_TYPE_UINT16);
        vkCmdDrawIndexed(cmd, s_veg.tree_icount, s_veg.tree_count, 0, 0, 0);
    }
    if(s_veg.rock_shadow_pipe != VK_NULL_HANDLE){
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s_veg.rock_shadow_pipe);
        vkCmdPushConstants(cmd, s_veg.scene->pipe_layout, VEG_PC_STAGES, 0, sizeof pcShadow, pcShadow);
        vkCmdBindVertexBuffers(cmd, 0, 2, rbufs, roffs);
        vkCmdDraw(cmd, ROCK_VERTS, ROCK_COUNT, 0, 0);
    }
    if(s_veg.tree_pipe != VK_NULL_HANDLE && s_veg.tree_count > 0){
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s_veg.tree_pipe);
        vkCmdPushConstants(cmd, s_veg.scene->pipe_layout, VEG_PC_STAGES, 0, sizeof pcNormal, pcNormal);
        vkCmdBindVertexBuffers(cmd, 0, 2, tbufs, toffs);
        vkCmdBindIndexBuffer(cmd, s_veg.tree_ib.buf, 0, VK_INDEX_TYPE_UINT16);
        vkCmdDrawIndexed(cmd, s_veg.tree_icount, s_veg.tree_count, 0, 0, 0);
    }
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s_veg.grass_pipe);
    vkCmdBindVertexBuffers(cmd, 0, 1, &s_veg.grass_buf[slot].buf, &zero);
    vkCmdDraw(cmd, GRASS_VERTS, GRASS_COUNT, 0, 0);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s_veg.rock_pipe);
    vkCmdPushConstants(cmd, s_veg.scene->pipe_layout, VEG_PC_STAGES, 0, sizeof pcNormal, pcNormal);
    vkCmdBindVertexBuffers(cmd, 0, 2, rbufs, roffs);
    vkCmdDraw(cmd, ROCK_VERTS, ROCK_COUNT, 0, 0);
}

float veg_min_tree_dist(v3 p){
    float best = 1.0e18f;
    for(uint32_t i = 0; i < s_veg.tree_count; i++){
        float dx = s_veg.trees[i].p[0] - p.x;
        float dz = s_veg.trees[i].p[2] - p.z;
        float d2 = dx*dx + dz*dz;
        if(d2 < best) best = d2;
    }
    return sqrtf(best);
}

void veg_destroy(void){
    VkCore *c = s_veg.core;
    if(!c || !c->device) return;
    if(s_veg.grass_pipe != VK_NULL_HANDLE) vkDestroyPipeline(c->device, s_veg.grass_pipe, NULL);
    if(s_veg.rock_pipe != VK_NULL_HANDLE) vkDestroyPipeline(c->device, s_veg.rock_pipe, NULL);
    if(s_veg.tree_pipe != VK_NULL_HANDLE) vkDestroyPipeline(c->device, s_veg.tree_pipe, NULL);
    if(s_veg.rock_shadow_pipe != VK_NULL_HANDLE) vkDestroyPipeline(c->device, s_veg.rock_shadow_pipe, NULL);
    if(s_veg.tree_shadow_pipe != VK_NULL_HANDLE) vkDestroyPipeline(c->device, s_veg.tree_shadow_pipe, NULL);
    for(uint32_t i = 0; i < VISTA_FRAMES; i++){
        vkc_buffer_destroy(c, &s_veg.grass_buf[i]);
        vkc_buffer_destroy(c, &s_veg.rock_buf[i]);
    }
    vkc_buffer_destroy(c, &s_veg.rock_vb);
    vkc_buffer_destroy(c, &s_veg.tree_vb);
    vkc_buffer_destroy(c, &s_veg.tree_ib);
    vkc_buffer_destroy(c, &s_veg.tree_instbuf);
    s_veg.grass_pipe = VK_NULL_HANDLE;
    s_veg.rock_pipe = VK_NULL_HANDLE;
    s_veg.tree_pipe = VK_NULL_HANDLE;
    s_veg.rock_shadow_pipe = VK_NULL_HANDLE;
    s_veg.tree_shadow_pipe = VK_NULL_HANDLE;
    s_veg.scene = NULL;
    s_veg.core = NULL;
}
