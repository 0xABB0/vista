#include "vista.h"
#include <string.h>
#include <stdio.h>

#define RCHK(x) do { VkResult rchk_r = (x); if (rchk_r != VK_SUCCESS) { plat_log("render: %s failed (%d) at %s:%d", #x, (int)rchk_r, __FILE__, __LINE__); return -1; } } while (0)

#define LUT_T_W 256
#define LUT_T_H 64
#define LUT_MS_N 32
#define LUT_SKY_W 192
#define LUT_SKY_H 108
#define BLOOM_MIPS 6
#define SHADOW_CASC_MAX 3
#define SHADOW_ZPAD 600.0f
#define SHADOW_ZPAD_BEHIND 120.0f
#define BLOOM_PC_STAGES (VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT | \
                         VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)

static struct {
    VkCore *core;
    uint32_t width, height, layers;
    VkFormat hdr_format;
    VImg msaa_color;
    VImg msaa_depth;
    VImg hdr_resolve;
    Pass terrain_pass;
    Pass scene_pass;
    VkPipeline post_pipe;
    VkDescriptorSet post_set;
    VkRenderPass final_rp;
    VImg lut_t, lut_ms, lut_sky;
    Pass t_pass, ms_pass, sky_pass;
    VkPipeline t_pipe, ms_pipe, sky_pipe;
    VkDescriptorSet ms_set, sky_set;
    VImg bloom_mip[BLOOM_MIPS];
    Pass bloom_down_pass[BLOOM_MIPS];
    Pass bloom_up_pass[BLOOM_MIPS - 1];
    VkPipeline bloom_down_pipe, bloom_up_pipe;
    VkDescriptorSet bloom_down_set[BLOOM_MIPS];
    VkDescriptorSet bloom_up_set[BLOOM_MIPS - 1];
    VImg shadow_img;
    VkImageView shadow_layer_view[SHADOW_CASC_MAX];
    Pass shadow_pass[SHADOW_CASC_MAX];
    uint32_t ncasc, shadow_res;
    float casc_radius[SHADOW_CASC_MAX];
} g_rd;

static int make_fullscreen_pipe_blend(VkCore *c, const char *frag, VkRenderPass rp, bool mv,
                                      bool additive, VkPipeline *out)
{
    VkShaderModule vs = vkc_shader(c, mv ? "shaders/fullscreen.vert.mv.spv" : "shaders/fullscreen.vert.spv");
    char fname[96];
    snprintf(fname, sizeof fname, "shaders/%s%s.spv", frag, mv ? ".mv" : "");
    VkShaderModule fs = vkc_shader(c, fname);
    if (vs == VK_NULL_HANDLE || fs == VK_NULL_HANDLE) {
        if (vs) vkDestroyShaderModule(c->device, vs, 0);
        if (fs) vkDestroyShaderModule(c->device, fs, 0);
        return -1;
    }
    VkPipelineShaderStageCreateInfo stages[2] = {
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vs, .pName = "main",
          .pSpecializationInfo = vkc_tier_spec(c) },
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fs, .pName = "main",
          .pSpecializationInfo = vkc_tier_spec(c) },
    };
    VkPipelineVertexInputStateCreateInfo vin = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
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
        .lineWidth = 1.0f,
    };
    VkPipelineMultisampleStateCreateInfo ms = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    VkPipelineDepthStencilStateCreateInfo ds = { .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    VkPipelineColorBlendAttachmentState cba = {
        .blendEnable = additive,
        .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };
    VkPipelineColorBlendStateCreateInfo cb = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &cba,
    };
    VkDynamicState dyn[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dsi = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2,
        .pDynamicStates = dyn,
    };
    VkGraphicsPipelineCreateInfo pci = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2,
        .pStages = stages,
        .pVertexInputState = &vin,
        .pInputAssemblyState = &ia,
        .pViewportState = &vps,
        .pRasterizationState = &rs,
        .pMultisampleState = &ms,
        .pDepthStencilState = &ds,
        .pColorBlendState = &cb,
        .pDynamicState = &dsi,
        .layout = g_scene.pipe_layout,
        .renderPass = rp,
        .subpass = 0,
    };
    VkResult r = vkCreateGraphicsPipelines(c->device, c->pcache, 1, &pci, 0, out);
    vkDestroyShaderModule(c->device, vs, 0);
    vkDestroyShaderModule(c->device, fs, 0);
    RCHK(r);
    return 0;
}

static int make_fullscreen_pipe(VkCore *c, const char *frag, VkRenderPass rp, bool mv, VkPipeline *out)
{
    return make_fullscreen_pipe_blend(c, frag, rp, mv, false, out);
}

static void fullscreen_draw(VkCommandBuffer cmd, const Pass *p, VkPipeline pipe,
                            VkDescriptorSet set0, VkDescriptorSet set1)
{
    vkc_pass_begin(cmd, p, 0, 0);
    VkViewport vp = { 0.0f, 0.0f, (float)p->width, (float)p->height, 0.0f, 1.0f };
    VkRect2D sc = { { 0, 0 }, { p->width, p->height } };
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &sc);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    if (set0 != VK_NULL_HANDLE)
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_scene.pipe_layout, 0, 1, &set0, 0, 0);
    if (set1 != VK_NULL_HANDLE)
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_scene.pipe_layout, 1, 1, &set1, 0, 0);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);
}

static void bloom_sync(VkCommandBuffer cmd, VkImage src, VkImage dst, bool load_dst)
{
    VkImageMemoryBarrier b[2];
    uint32_t n = 0;
    memset(b, 0, sizeof b);
    if (src != VK_NULL_HANDLE) {
        b[n] = (VkImageMemoryBarrier){
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = src,
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, VK_REMAINING_ARRAY_LAYERS },
        };
        n++;
    }
    if (dst != VK_NULL_HANDLE) {
        b[n] = (VkImageMemoryBarrier){
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = load_dst ? VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT : 0,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout = load_dst ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = load_dst ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = dst,
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, VK_REMAINING_ARRAY_LAYERS },
        };
        n++;
    }
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         0, 0, 0, 0, 0, n, b);
}

static void bloom_draw(VkCommandBuffer cmd, const Pass *p, VkPipeline pipe, VkDescriptorSet set0,
                       VkDescriptorSet texset, float src_w, float src_h, float karis)
{
    vkc_pass_begin(cmd, p, 0, 0);
    VkViewport vp = { 0.0f, 0.0f, (float)p->width, (float)p->height, 0.0f, 1.0f };
    VkRect2D sc = { { 0, 0 }, { p->width, p->height } };
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &sc);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    if (set0 != VK_NULL_HANDLE)
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_scene.pipe_layout, 0, 1, &set0, 0, 0);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_scene.pipe_layout, 1, 1, &texset, 0, 0);
    float pcv[4] = { 1.0f / src_w, 1.0f / src_h, karis, 0.0f };
    vkCmdPushConstants(cmd, g_scene.pipe_layout, BLOOM_PC_STAGES, 0, sizeof pcv, pcv);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);
}

static void bloom_mip_size(uint32_t w, uint32_t h, uint32_t i, uint32_t *mw, uint32_t *mh)
{
    uint32_t sw = w >> (i + 1);
    uint32_t sh = h >> (i + 1);
    *mw = sw ? sw : 1;
    *mh = sh ? sh : 1;
}

static VkFormat pick_hdr_format(VkCore *c)
{
    if (c->tier == 0) {
        VkFormatProperties fp;
        vkGetPhysicalDeviceFormatProperties(c->phys, VK_FORMAT_B10G11R11_UFLOAT_PACK32, &fp);
        VkFormatFeatureFlags need = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT |
                                    VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
        if ((fp.optimalTilingFeatures & need) == need) {
            VkImageFormatProperties ip;
            VkResult r = vkGetPhysicalDeviceImageFormatProperties(
                c->phys, VK_FORMAT_B10G11R11_UFLOAT_PACK32, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT, 0, &ip);
            if (r == VK_SUCCESS && (ip.sampleCounts & c->msaa))
                return VK_FORMAT_B10G11R11_UFLOAT_PACK32;
        }
    }
    return VK_FORMAT_R16G16B16A16_SFLOAT;
}

static int create_targets(VkCore *c, uint32_t w, uint32_t h)
{
    uint32_t view_mask = g_rd.layers == 2 ? 0x3 : 0;
    bool split = c->tess_split_pass;
    if (vkc_image_create(c, w, h, g_rd.layers, 1, g_rd.hdr_format, c->msaa,
                         VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, !split, &g_rd.msaa_color))
        return -1;
    if (vkc_image_create(c, w, h, g_rd.layers, 1, c->depth_format, c->msaa,
                         VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, !split, &g_rd.msaa_depth))
        return -1;
    if (vkc_image_create(c, w, h, g_rd.layers, 1, g_rd.hdr_format, VK_SAMPLE_COUNT_1_BIT,
                         VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, false,
                         &g_rd.hdr_resolve))
        return -1;
    if (split) {
        if (vkc_pass_scene_pre(&g_rd.terrain_pass, c, g_rd.hdr_format, g_rd.msaa_color.view,
                               g_rd.msaa_depth.view, w, h, view_mask))
            return -1;
        if (vkc_pass_scene_main(&g_rd.scene_pass, c, g_rd.hdr_format, g_rd.msaa_color.view,
                                g_rd.msaa_depth.view, g_rd.hdr_resolve.view, w, h, view_mask))
            return -1;
    } else if (vkc_pass_scene(&g_rd.scene_pass, c, g_rd.hdr_format, g_rd.msaa_color.view,
                              g_rd.msaa_depth.view, g_rd.hdr_resolve.view, w, h, view_mask)) {
        return -1;
    }
    if (c->tier != 0) {
        for (uint32_t i = 0; i < BLOOM_MIPS; i++) {
            uint32_t mw, mh;
            bloom_mip_size(w, h, i, &mw, &mh);
            if (vkc_image_create(c, mw, mh, g_rd.layers, 1, VK_FORMAT_R16G16B16A16_SFLOAT,
                                 VK_SAMPLE_COUNT_1_BIT,
                                 VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                 false, &g_rd.bloom_mip[i]))
                return -1;
            if (vkc_pass_color(&g_rd.bloom_down_pass[i], c, VK_FORMAT_R16G16B16A16_SFLOAT,
                               g_rd.bloom_mip[i].view, mw, mh, view_mask, false))
                return -1;
        }
        for (uint32_t i = 0; i < BLOOM_MIPS - 1; i++) {
            uint32_t mw, mh;
            bloom_mip_size(w, h, i, &mw, &mh);
            if (vkc_pass_color(&g_rd.bloom_up_pass[i], c, VK_FORMAT_R16G16B16A16_SFLOAT,
                               g_rd.bloom_mip[i].view, mw, mh, view_mask, true))
                return -1;
        }
    }
    g_rd.width = w;
    g_rd.height = h;
    return 0;
}

static void destroy_targets(void)
{
    VkCore *c = g_rd.core;
    if (!c || !c->device) return;
    vkc_pass_destroy(&g_rd.terrain_pass);
    vkc_pass_destroy(&g_rd.scene_pass);
    for (uint32_t i = 0; i < BLOOM_MIPS; i++) {
        vkc_pass_destroy(&g_rd.bloom_down_pass[i]);
        if (i < BLOOM_MIPS - 1)
            vkc_pass_destroy(&g_rd.bloom_up_pass[i]);
        vkc_image_destroy(c, &g_rd.bloom_mip[i]);
    }
    vkc_image_destroy(c, &g_rd.msaa_color);
    vkc_image_destroy(c, &g_rd.msaa_depth);
    vkc_image_destroy(c, &g_rd.hdr_resolve);
}

static int create_shadow_targets(VkCore *c)
{
    if (c->tier == 0) {
        g_rd.ncasc = 2;
        g_rd.shadow_res = 1024;
        g_rd.casc_radius[0] = 20.0f;
        g_rd.casc_radius[1] = 90.0f;
        g_rd.casc_radius[2] = 90.0f;
    } else {
        g_rd.ncasc = 3;
        g_rd.shadow_res = 2048;
        g_rd.casc_radius[0] = 18.0f;
        g_rd.casc_radius[1] = 60.0f;
        g_rd.casc_radius[2] = 200.0f;
    }
    if (vkc_image_create(c, g_rd.shadow_res, g_rd.shadow_res, g_rd.ncasc, 1, VK_FORMAT_D16_UNORM,
                         VK_SAMPLE_COUNT_1_BIT,
                         VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                         false, &g_rd.shadow_img))
        return -1;
    for (uint32_t ci = 0; ci < g_rd.ncasc; ci++) {
        VkImageViewCreateInfo vci = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = g_rd.shadow_img.img,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = VK_FORMAT_D16_UNORM,
            .subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, ci, 1 },
        };
        RCHK(vkCreateImageView(c->device, &vci, 0, &g_rd.shadow_layer_view[ci]));
        if (vkc_pass_depth(&g_rd.shadow_pass[ci], c, VK_FORMAT_D16_UNORM,
                           g_rd.shadow_layer_view[ci], g_rd.shadow_res, g_rd.shadow_res))
            return -1;
    }
    return 0;
}

int render_init(VkCore *c, uint32_t w, uint32_t h, uint32_t layers)
{
    memset(&g_rd, 0, sizeof g_rd);
    g_rd.core = c;
    g_rd.layers = layers;
    g_rd.hdr_format = pick_hdr_format(c);
    plat_log("render: hdr format %d, %ux%u, %u layer(s)", (int)g_rd.hdr_format, w, h, layers);
    if (create_shadow_targets(c))
        return -1;
    return create_targets(c, w, h);
}

VkRenderPass render_scene_rp(void)
{
    return g_rd.scene_pass.rp;
}

VkRenderPass render_terrain_rp(void)
{
    return g_rd.core->tess_split_pass ? g_rd.terrain_pass.rp : g_rd.scene_pass.rp;
}

VkRenderPass render_shadow_rp(void)
{
    return g_rd.shadow_pass[0].rp;
}

VkImageView render_shadowmap_view(void)
{
    return g_rd.shadow_img.view;
}

void render_fill_cascades(v3 campos, v3 sundir, FrameUBO *ubo)
{
    if (!g_rd.ncasc) return;
    v3 L = v3norm(sundir);
    v3 up = fabsf(L.y) < 0.95f ? (v3){ 0.0f, 1.0f, 0.0f } : (v3){ 1.0f, 0.0f, 0.0f };
    m4 rot = m4look((v3){ 0.0f, 0.0f, 0.0f }, v3scale(L, -1.0f), up);
    for (uint32_t ci = 0; ci < SHADOW_CASC_MAX; ci++) {
        uint32_t src = ci < g_rd.ncasc ? ci : g_rd.ncasc - 1;
        float rad = g_rd.casc_radius[src];
        float cx = rot.m[0] * campos.x + rot.m[4] * campos.y + rot.m[8] * campos.z;
        float cy = rot.m[1] * campos.x + rot.m[5] * campos.y + rot.m[9] * campos.z;
        float cz = rot.m[2] * campos.x + rot.m[6] * campos.y + rot.m[10] * campos.z;
        float texel = 2.0f * rad / (float)g_rd.shadow_res;
        cx = floorf(cx / texel) * texel;
        cy = floorf(cy / texel) * texel;
        float zn = -cz - SHADOW_ZPAD;
        float zf = -cz + rad + SHADOW_ZPAD_BEHIND;
        m4 proj = m4ortho(cx - rad, cx + rad, cy - rad, cy + rad, zn, zf);
        ubo->cascade_vp[ci] = m4mul(proj, rot);
        ubo->cascade_radii[ci] = rad;
        ubo->cascade_texel[ci] = texel;
    }
    ubo->cascade_radii[3] = (float)g_rd.ncasc;
    ubo->cascade_texel[3] = 0.0f;
}

static uint32_t post_image_info(VkDescriptorImageInfo *ii)
{
    ii[0] = (VkDescriptorImageInfo){ g_rd.core->sampler_clamp, g_rd.hdr_resolve.view,
                                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    if (g_rd.core->tier == 0)
        return 1;
    ii[1] = (VkDescriptorImageInfo){ g_rd.core->sampler_clamp, g_rd.bloom_mip[0].view,
                                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    return 2;
}

static void bloom_src_info(uint32_t pass_idx, bool up, VkDescriptorImageInfo *ii)
{
    VkImageView src;
    if (up)
        src = g_rd.bloom_mip[pass_idx + 1].view;
    else
        src = pass_idx == 0 ? g_rd.hdr_resolve.view : g_rd.bloom_mip[pass_idx - 1].view;
    *ii = (VkDescriptorImageInfo){ g_rd.core->sampler_clamp, src,
                                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
}

int render_post_init(VkCore *c, VkRenderPass final_rp)
{
    g_rd.final_rp = final_rp;
    bool mv = g_rd.layers == 2;
    if (make_fullscreen_pipe(c, "post_final.frag", final_rp, mv, &g_rd.post_pipe))
        return -1;
    VkDescriptorImageInfo ii[2];
    uint32_t nii = post_image_info(ii);
    if (scene_alloc_texset(c, ii, nii, &g_rd.post_set))
        return -1;
    if (c->tier != 0) {
        if (make_fullscreen_pipe(c, "bloom_down.frag", g_rd.bloom_down_pass[0].rp, mv, &g_rd.bloom_down_pipe))
            return -1;
        if (make_fullscreen_pipe_blend(c, "bloom_up.frag", g_rd.bloom_up_pass[0].rp, mv, true, &g_rd.bloom_up_pipe))
            return -1;
        for (uint32_t i = 0; i < BLOOM_MIPS; i++) {
            VkDescriptorImageInfo bi;
            bloom_src_info(i, false, &bi);
            if (scene_alloc_texset(c, &bi, 1, &g_rd.bloom_down_set[i]))
                return -1;
        }
        for (uint32_t i = 0; i < BLOOM_MIPS - 1; i++) {
            VkDescriptorImageInfo bi;
            bloom_src_info(i, true, &bi);
            if (scene_alloc_texset(c, &bi, 1, &g_rd.bloom_up_set[i]))
                return -1;
        }
    }

    if (vkc_image_create(c, LUT_T_W, LUT_T_H, 1, 1, VK_FORMAT_R16G16B16A16_SFLOAT, VK_SAMPLE_COUNT_1_BIT,
                         VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, false, &g_rd.lut_t))
        return -1;
    if (vkc_image_create(c, LUT_MS_N, LUT_MS_N, 1, 1, VK_FORMAT_R16G16B16A16_SFLOAT, VK_SAMPLE_COUNT_1_BIT,
                         VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, false, &g_rd.lut_ms))
        return -1;
    if (vkc_image_create(c, LUT_SKY_W, LUT_SKY_H, 1, 1, VK_FORMAT_R16G16B16A16_SFLOAT, VK_SAMPLE_COUNT_1_BIT,
                         VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, false, &g_rd.lut_sky))
        return -1;
    if (vkc_pass_color(&g_rd.t_pass, c, VK_FORMAT_R16G16B16A16_SFLOAT, g_rd.lut_t.view, LUT_T_W, LUT_T_H, 0, false))
        return -1;
    if (vkc_pass_color(&g_rd.ms_pass, c, VK_FORMAT_R16G16B16A16_SFLOAT, g_rd.lut_ms.view, LUT_MS_N, LUT_MS_N, 0, false))
        return -1;
    if (vkc_pass_color(&g_rd.sky_pass, c, VK_FORMAT_R16G16B16A16_SFLOAT, g_rd.lut_sky.view, LUT_SKY_W, LUT_SKY_H, 0, false))
        return -1;
    if (make_fullscreen_pipe(c, "lut_transmittance.frag", g_rd.t_pass.rp, false, &g_rd.t_pipe))
        return -1;
    if (make_fullscreen_pipe(c, "lut_multiscatter.frag", g_rd.ms_pass.rp, false, &g_rd.ms_pipe))
        return -1;
    if (make_fullscreen_pipe(c, "lut_skyview.frag", g_rd.sky_pass.rp, false, &g_rd.sky_pipe))
        return -1;
    VkDescriptorImageInfo tinfo = { c->sampler_clamp, g_rd.lut_t.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    if (scene_alloc_texset(c, &tinfo, 1, &g_rd.ms_set))
        return -1;
    VkDescriptorImageInfo svinfo[2] = {
        { c->sampler_clamp, g_rd.lut_t.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
        { c->sampler_clamp, g_rd.lut_ms.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
    };
    if (scene_alloc_texset(c, svinfo, 2, &g_rd.sky_set))
        return -1;
    VkCommandBuffer cmd = vkc_begin_once(c);
    if (cmd == VK_NULL_HANDLE)
        return -1;
    fullscreen_draw(cmd, &g_rd.t_pass, g_rd.t_pipe, VK_NULL_HANDLE, VK_NULL_HANDLE);
    vkc_end_once(c, cmd);
    cmd = vkc_begin_once(c);
    if (cmd == VK_NULL_HANDLE)
        return -1;
    fullscreen_draw(cmd, &g_rd.ms_pass, g_rd.ms_pipe, VK_NULL_HANDLE, g_rd.ms_set);
    vkc_end_once(c, cmd);
    scene_write_luts(c, g_rd.lut_t.view, g_rd.lut_ms.view, g_rd.lut_sky.view);
    return 0;
}

int render_resize(uint32_t w, uint32_t h)
{
    VkCore *c = g_rd.core;
    if (!c) return -1;
    if (w == g_rd.width && h == g_rd.height) return 0;
    RCHK(vkDeviceWaitIdle(c->device));
    destroy_targets();
    if (create_targets(c, w, h)) return -1;
    if (g_rd.post_set) {
        VkDescriptorImageInfo ii[2];
        uint32_t nii = post_image_info(ii);
        scene_write_texset(c, g_rd.post_set, ii, nii);
    }
    if (c->tier != 0 && g_rd.bloom_down_set[0]) {
        for (uint32_t i = 0; i < BLOOM_MIPS; i++) {
            VkDescriptorImageInfo bi;
            bloom_src_info(i, false, &bi);
            scene_write_texset(c, g_rd.bloom_down_set[i], &bi, 1);
        }
        for (uint32_t i = 0; i < BLOOM_MIPS - 1; i++) {
            VkDescriptorImageInfo bi;
            bloom_src_info(i, true, &bi);
            scene_write_texset(c, g_rd.bloom_up_set[i], &bi, 1);
        }
    }
    return 0;
}

void render_record(VkCommandBuffer cmd, uint32_t slot, VkFramebuffer final_fb, uint32_t fw, uint32_t fh)
{
    VkClearValue dclear;
    memset(&dclear, 0, sizeof dclear);
    dclear.depthStencil.depth = 1.0f;
    for (uint32_t ci = 0; ci < g_rd.ncasc; ci++) {
        vkc_pass_begin(cmd, &g_rd.shadow_pass[ci], &dclear, 1);
        scene_record_shadow(cmd, slot, ci, g_rd.shadow_res);
        vkCmdEndRenderPass(cmd);
    }
    fullscreen_draw(cmd, &g_rd.sky_pass, g_rd.sky_pipe, g_scene.set0[slot], g_rd.sky_set);
    VkClearValue clears[3];
    memset(clears, 0, sizeof clears);
    clears[0].color.float32[3] = 1.0f;
    clears[1].depthStencil.depth = 1.0f;
    if (g_rd.core->tess_split_pass) {
        vkc_pass_begin(cmd, &g_rd.terrain_pass, clears, 2);
        scene_record_terrain(cmd, slot, g_rd.width, g_rd.height);
        vkCmdEndRenderPass(cmd);
        vkc_pass_begin(cmd, &g_rd.scene_pass, 0, 0);
        scene_record_rest(cmd, slot, g_rd.width, g_rd.height);
        vkCmdEndRenderPass(cmd);
    } else {
        vkc_pass_begin(cmd, &g_rd.scene_pass, clears, 3);
        scene_record(cmd, slot, g_rd.width, g_rd.height);
        vkCmdEndRenderPass(cmd);
    }
    if (g_rd.core->tier != 0) {
        for (uint32_t i = 0; i < BLOOM_MIPS; i++) {
            VkImage src = i == 0 ? g_rd.hdr_resolve.img : g_rd.bloom_mip[i - 1].img;
            float sw = i == 0 ? (float)g_rd.width : (float)g_rd.bloom_mip[i - 1].w;
            float sh = i == 0 ? (float)g_rd.height : (float)g_rd.bloom_mip[i - 1].h;
            bloom_sync(cmd, src, g_rd.bloom_mip[i].img, false);
            bloom_draw(cmd, &g_rd.bloom_down_pass[i], g_rd.bloom_down_pipe, g_scene.set0[slot],
                       g_rd.bloom_down_set[i], sw, sh, i == 0 ? 1.0f : 0.0f);
        }
        for (uint32_t i = BLOOM_MIPS - 1; i > 0; i--) {
            float sw = (float)g_rd.bloom_mip[i].w;
            float sh = (float)g_rd.bloom_mip[i].h;
            bloom_sync(cmd, g_rd.bloom_mip[i].img, g_rd.bloom_mip[i - 1].img, true);
            bloom_draw(cmd, &g_rd.bloom_up_pass[i - 1], g_rd.bloom_up_pipe, VK_NULL_HANDLE,
                       g_rd.bloom_up_set[i - 1], sw, sh, 0.0f);
        }
        bloom_sync(cmd, g_rd.bloom_mip[0].img, VK_NULL_HANDLE, false);
    }
    VkRenderPassBeginInfo rbi = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = g_rd.final_rp,
        .framebuffer = final_fb,
        .renderArea = { { 0, 0 }, { fw, fh } },
    };
    vkCmdBeginRenderPass(cmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport vp = { 0.0f, 0.0f, (float)fw, (float)fh, 0.0f, 1.0f };
    VkRect2D sc = { { 0, 0 }, { fw, fh } };
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &sc);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_rd.post_pipe);
    VkDescriptorSet sets[2] = { g_scene.set0[slot], g_rd.post_set };
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_scene.pipe_layout, 0, 2, sets, 0, 0);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);
}

void render_destroy(void)
{
    VkCore *c = g_rd.core;
    if (!c || !c->device) return;
    VkResult wr = vkDeviceWaitIdle(c->device);
    if (wr != VK_SUCCESS)
        plat_log("render: vkDeviceWaitIdle failed (%d)", (int)wr);
    if (g_rd.post_pipe) vkDestroyPipeline(c->device, g_rd.post_pipe, 0);
    if (g_rd.bloom_down_pipe) vkDestroyPipeline(c->device, g_rd.bloom_down_pipe, 0);
    if (g_rd.bloom_up_pipe) vkDestroyPipeline(c->device, g_rd.bloom_up_pipe, 0);
    if (g_rd.t_pipe) vkDestroyPipeline(c->device, g_rd.t_pipe, 0);
    if (g_rd.ms_pipe) vkDestroyPipeline(c->device, g_rd.ms_pipe, 0);
    if (g_rd.sky_pipe) vkDestroyPipeline(c->device, g_rd.sky_pipe, 0);
    vkc_pass_destroy(&g_rd.t_pass);
    vkc_pass_destroy(&g_rd.ms_pass);
    vkc_pass_destroy(&g_rd.sky_pass);
    vkc_image_destroy(c, &g_rd.lut_t);
    vkc_image_destroy(c, &g_rd.lut_ms);
    vkc_image_destroy(c, &g_rd.lut_sky);
    for (uint32_t ci = 0; ci < g_rd.ncasc; ci++) {
        vkc_pass_destroy(&g_rd.shadow_pass[ci]);
        if (g_rd.shadow_layer_view[ci])
            vkDestroyImageView(c->device, g_rd.shadow_layer_view[ci], 0);
    }
    vkc_image_destroy(c, &g_rd.shadow_img);
    destroy_targets();
    memset(&g_rd, 0, sizeof g_rd);
}
