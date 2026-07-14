#include "vista.h"
#include <string.h>

#define SCHK(x) do { VkResult schk_r = (x); if (schk_r != VK_SUCCESS) { plat_log("scene: %s failed (%d) at %s:%d", #x, (int)schk_r, __FILE__, __LINE__); return -1; } } while (0)

Scene g_scene;

void scene_write_texset(VkCore *c, VkDescriptorSet set, const VkDescriptorImageInfo *imgs, uint32_t n)
{
    VkDescriptorImageInfo padded[8];
    VkWriteDescriptorSet ws[8];
    memset(ws, 0, sizeof ws);
    for (uint32_t i = 0; i < 8; i++) {
        padded[i] = imgs[i < n ? i : n - 1];
        ws[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        ws[i].dstSet = set;
        ws[i].dstBinding = i;
        ws[i].descriptorCount = 1;
        ws[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        ws[i].pImageInfo = &padded[i];
    }
    vkUpdateDescriptorSets(c->device, 8, ws, 0, 0);
}

void scene_write_luts(VkCore *c, VkImageView t, VkImageView ms, VkImageView sky)
{
    VkDescriptorImageInfo imgs[3] = {
        { c->sampler_clamp, t, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
        { c->sampler_clamp, ms, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
        { c->sampler_clamp, sky, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
    };
    for (uint32_t s = 0; s < VISTA_FRAMES; s++) {
        VkWriteDescriptorSet ws[3];
        memset(ws, 0, sizeof ws);
        for (uint32_t j = 0; j < 3; j++) {
            ws[j].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            ws[j].dstSet = g_scene.set0[s];
            ws[j].dstBinding = 3 + j;
            ws[j].descriptorCount = 1;
            ws[j].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            ws[j].pImageInfo = &imgs[j];
        }
        vkUpdateDescriptorSets(c->device, 3, ws, 0, 0);
    }
}

int scene_alloc_texset(VkCore *c, const VkDescriptorImageInfo *imgs, uint32_t n, VkDescriptorSet *out)
{
    VkDescriptorSetAllocateInfo ai = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = c->dpool,
        .descriptorSetCount = 1,
        .pSetLayouts = &g_scene.texset_layout,
    };
    SCHK(vkAllocateDescriptorSets(c->device, &ai, out));
    scene_write_texset(c, *out, imgs, n);
    return 0;
}

static int scene_write_sets(VkCore *c)
{
    VImg *hm = terrain_heightmap_tex();
    if (!hm || !hm->view) {
        plat_log("scene: heightmap texture missing");
        return -1;
    }
    VImg *lm = terrain_lightmap_tex();
    if (!lm || !lm->view) {
        plat_log("scene: lightmap texture missing");
        return -1;
    }
    VImg *hz0 = terrain_horizon_tex(0);
    VImg *hz1 = terrain_horizon_tex(1);
    if (!hz0->view || !hz1->view) {
        plat_log("scene: horizon textures missing");
        return -1;
    }
    VkImageView smview = render_shadowmap_view();
    if (!smview) {
        plat_log("scene: shadow map view missing");
        return -1;
    }
    VkDescriptorImageInfo imgs[5] = {
        { c->sampler_clamp, hm->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
        { c->sampler_clamp, lm->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
        { c->sampler_clamp, hz0->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
        { c->sampler_clamp, hz1->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
        { c->sampler_shadow, smview, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
    };
    uint32_t bindings[5] = { 1, 2, 6, 7, 8 };
    for (uint32_t s = 0; s < VISTA_FRAMES; s++) {
        VkDescriptorBufferInfo bi = { g_scene.ubo[s].buf, 0, sizeof(FrameUBO) };
        VkWriteDescriptorSet ws[6];
        memset(ws, 0, sizeof ws);
        ws[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        ws[0].dstSet = g_scene.set0[s];
        ws[0].dstBinding = 0;
        ws[0].descriptorCount = 1;
        ws[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        ws[0].pBufferInfo = &bi;
        for (uint32_t j = 0; j < 5; j++) {
            ws[j + 1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            ws[j + 1].dstSet = g_scene.set0[s];
            ws[j + 1].dstBinding = bindings[j];
            ws[j + 1].descriptorCount = 1;
            ws[j + 1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            ws[j + 1].pImageInfo = &imgs[j];
        }
        vkUpdateDescriptorSets(c->device, 6, ws, 0, 0);
    }
    VkDescriptorImageInfo mats[8] = {
        { c->sampler_repeat, g_tex.grass_color.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
        { c->sampler_repeat, g_tex.grass_normal.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
        { c->sampler_repeat, g_tex.rock_color.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
        { c->sampler_repeat, g_tex.rock_normal.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
        { c->sampler_repeat, g_tex.dirt_color.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
        { c->sampler_repeat, g_tex.dirt_normal.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
        { c->sampler_repeat, g_tex.snow_color.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
        { c->sampler_repeat, g_tex.snow_normal.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
    };
    for (uint32_t i = 0; i < 8; i++)
        if (!mats[i].imageView) {
            plat_log("scene: material texture %u missing", i);
            return -1;
        }
    return scene_alloc_texset(c, mats, 8, &g_scene.material_set);
}

int scene_init(VkCore *c, VkRenderPass rp, bool multiview)
{
    memset(&g_scene, 0, sizeof g_scene);
    g_scene.core = c;
    g_scene.multiview = multiview;
    VkDescriptorSetLayoutBinding b[9];
    b[0] = (VkDescriptorSetLayoutBinding){ 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_ALL_GRAPHICS, 0 };
    for (uint32_t i = 1; i < 9; i++)
        b[i] = (VkDescriptorSetLayoutBinding){ i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_ALL_GRAPHICS, 0 };
    VkDescriptorSetLayoutCreateInfo li = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 9,
        .pBindings = b,
    };
    SCHK(vkCreateDescriptorSetLayout(c->device, &li, 0, &g_scene.set0_layout));
    VkDescriptorSetLayoutBinding tb[8];
    for (uint32_t i = 0; i < 8; i++)
        tb[i] = (VkDescriptorSetLayoutBinding){ i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_ALL_GRAPHICS, 0 };
    VkDescriptorSetLayoutCreateInfo tli = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 8,
        .pBindings = tb,
    };
    SCHK(vkCreateDescriptorSetLayout(c->device, &tli, 0, &g_scene.texset_layout));
    VkPushConstantRange pcr = { VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT |
                                VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 128 };
    VkDescriptorSetLayout layouts[2] = { g_scene.set0_layout, g_scene.texset_layout };
    VkPipelineLayoutCreateInfo pli = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 2,
        .pSetLayouts = layouts,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pcr,
    };
    SCHK(vkCreatePipelineLayout(c->device, &pli, 0, &g_scene.pipe_layout));
    for (uint32_t i = 0; i < VISTA_FRAMES; i++)
        if (vkc_buffer(c, sizeof(FrameUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                       &g_scene.ubo[i]))
            return -1;
    VkDescriptorSetLayout ls[VISTA_FRAMES];
    for (uint32_t i = 0; i < VISTA_FRAMES; i++)
        ls[i] = g_scene.set0_layout;
    VkDescriptorSetAllocateInfo dai = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = c->dpool,
        .descriptorSetCount = VISTA_FRAMES,
        .pSetLayouts = ls,
    };
    SCHK(vkAllocateDescriptorSets(c->device, &dai, g_scene.set0));
    if (terrain_init(c, render_terrain_rp(), multiview, &g_scene)) return -1;
    if (scene_write_sets(c)) return -1;
    if (sky_init(c, rp, multiview, &g_scene)) return -1;
    if (veg_init(c, rp, multiview, &g_scene)) return -1;
    if (water_init(c, rp, multiview, &g_scene)) return -1;
    return 0;
}

void scene_update_ubo(uint32_t slot, const FrameUBO *ubo)
{
    if (slot >= VISTA_FRAMES || !g_scene.ubo[slot].map) return;
    memcpy(g_scene.ubo[slot].map, ubo, sizeof *ubo);
}

static void scene_bind(VkCommandBuffer cmd, uint32_t slot, uint32_t w, uint32_t h)
{
    VkViewport vp = { 0.0f, 0.0f, (float)w, (float)h, 0.0f, 1.0f };
    VkRect2D sc = { { 0, 0 }, { w, h } };
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &sc);
    VkDescriptorSet sets[2] = { g_scene.set0[slot], g_scene.material_set };
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_scene.pipe_layout,
                            0, 2, sets, 0, 0);
}

void scene_record_terrain(VkCommandBuffer cmd, uint32_t slot, uint32_t w, uint32_t h)
{
    if (slot >= VISTA_FRAMES) return;
    scene_bind(cmd, slot, w, h);
    terrain_record(cmd, slot, (const FrameUBO *)g_scene.ubo[slot].map);
}

void scene_record_shadow(VkCommandBuffer cmd, uint32_t slot, uint32_t cascade, uint32_t res)
{
    if (slot >= VISTA_FRAMES || !g_scene.ubo[slot].map) return;
    VkViewport vp = { 0.0f, 0.0f, (float)res, (float)res, 0.0f, 1.0f };
    VkRect2D sc = { { 0, 0 }, { res, res } };
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &sc);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_scene.pipe_layout,
                            0, 1, &g_scene.set0[slot], 0, 0);
    const FrameUBO *ubo = (const FrameUBO *)g_scene.ubo[slot].map;
    terrain_record_shadow(cmd, ubo, cascade);
    veg_record_shadow(cmd, slot, ubo, cascade);
}

void scene_record_rest(VkCommandBuffer cmd, uint32_t slot, uint32_t w, uint32_t h)
{
    if (slot >= VISTA_FRAMES) return;
    scene_bind(cmd, slot, w, h);
    veg_record(cmd, slot);
    water_record(cmd, slot);
    sky_record(cmd, slot);
}

void scene_record(VkCommandBuffer cmd, uint32_t slot, uint32_t w, uint32_t h)
{
    scene_record_terrain(cmd, slot, w, h);
    scene_record_rest(cmd, slot, w, h);
}

void scene_destroy(void)
{
    VkCore *c = g_scene.core;
    if (!c || !c->device) return;
    VkResult wr = vkDeviceWaitIdle(c->device);
    if (wr != VK_SUCCESS)
        plat_log("scene: vkDeviceWaitIdle failed (%d)", (int)wr);
    water_destroy();
    veg_destroy();
    sky_destroy();
    terrain_destroy();
    for (uint32_t i = 0; i < VISTA_FRAMES; i++)
        vkc_buffer_destroy(c, &g_scene.ubo[i]);
    if (g_scene.set0[0]) {
        VkResult fr = vkFreeDescriptorSets(c->device, c->dpool, VISTA_FRAMES, g_scene.set0);
        if (fr != VK_SUCCESS)
            plat_log("scene: vkFreeDescriptorSets failed (%d)", (int)fr);
    }
    if (g_scene.material_set) {
        VkResult fr = vkFreeDescriptorSets(c->device, c->dpool, 1, &g_scene.material_set);
        if (fr != VK_SUCCESS)
            plat_log("scene: material set free failed (%d)", (int)fr);
    }
    if (g_scene.pipe_layout) vkDestroyPipelineLayout(c->device, g_scene.pipe_layout, 0);
    if (g_scene.set0_layout) vkDestroyDescriptorSetLayout(c->device, g_scene.set0_layout, 0);
    if (g_scene.texset_layout) vkDestroyDescriptorSetLayout(c->device, g_scene.texset_layout, 0);
    memset(&g_scene, 0, sizeof g_scene);
}
