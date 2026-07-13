#include "vista.h"

#define WATER_GRID 128
#define WATER_SIDE (WATER_GRID + 1)
#define WATER_IDX (WATER_GRID * WATER_GRID * 6)

static struct {
    VkCore *core;
    VkPipelineLayout layout;
    VkPipeline pipe;
    VBuf ib;
} g_water;

static VkPipelineShaderStageCreateInfo stage_info(VkShaderStageFlagBits s, VkShaderModule m)
{
    return (VkPipelineShaderStageCreateInfo){
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = s,
        .module = m,
        .pName = "main"
    };
}

static int create_pipeline(VkCore *c, VkRenderPass rp, bool multiview)
{
    VkShaderModule vs = vkc_shader(c, multiview ? "shaders/water.vert.mv.spv" : "shaders/water.vert.spv");
    VkShaderModule fs = vkc_shader(c, multiview ? "shaders/water.frag.mv.spv" : "shaders/water.frag.spv");
    VkResult r = VK_ERROR_INITIALIZATION_FAILED;
    if (vs != VK_NULL_HANDLE && fs != VK_NULL_HANDLE)
    {
        VkPipelineShaderStageCreateInfo stages[2] = {
            stage_info(VK_SHADER_STAGE_VERTEX_BIT, vs),
            stage_info(VK_SHADER_STAGE_FRAGMENT_BIT, fs)
        };
        stages[0].pSpecializationInfo = vkc_tier_spec(c);
        stages[1].pSpecializationInfo = vkc_tier_spec(c);
        VkPipelineVertexInputStateCreateInfo vin = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO
        };
        VkPipelineInputAssemblyStateCreateInfo ia = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
        };
        VkPipelineViewportStateCreateInfo vps = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .viewportCount = 1,
            .scissorCount = 1
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
            .rasterizationSamples = c->msaa
        };
        VkPipelineDepthStencilStateCreateInfo ds = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .depthTestEnable = VK_TRUE,
            .depthWriteEnable = VK_TRUE,
            .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL
        };
        VkPipelineColorBlendAttachmentState cba = {
            .blendEnable = VK_TRUE,
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
            .attachmentCount = 1,
            .pAttachments = &cba
        };
        VkDynamicState dyn[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dsi = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .dynamicStateCount = 2,
            .pDynamicStates = dyn
        };
        VkGraphicsPipelineCreateInfo gp = {
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
            .layout = g_water.layout,
            .renderPass = rp,
            .subpass = 0
        };
        r = vkCreateGraphicsPipelines(c->device, c->pcache, 1, &gp, NULL, &g_water.pipe);
    }
    if (vs != VK_NULL_HANDLE) vkDestroyShaderModule(c->device, vs, NULL);
    if (fs != VK_NULL_HANDLE) vkDestroyShaderModule(c->device, fs, NULL);
    if (r != VK_SUCCESS)
    {
        plat_log("water: pipeline creation failed (%d)", (int)r);
        return -1;
    }
    return 0;
}

int water_init(VkCore *c, VkRenderPass rp, bool multiview, const Scene *scene)
{
    g_water.core = c;
    g_water.layout = scene->pipe_layout;
    if (vkc_buffer(c, WATER_IDX * sizeof(uint16_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                   &g_water.ib) != 0)
    {
        plat_log("water: index buffer creation failed");
        return -1;
    }
    void *map = g_water.ib.map;
    if (!map)
    {
        VkResult mr = vkMapMemory(c->device, g_water.ib.mem, 0, VK_WHOLE_SIZE, 0, &map);
        if (mr != VK_SUCCESS)
        {
            plat_log("water: index buffer map failed (%d)", (int)mr);
            return -1;
        }
    }
    uint16_t *idx = (uint16_t *)map;
    uint32_t k = 0;
    for (int z = 0; z < WATER_GRID; z++)
    {
        for (int x = 0; x < WATER_GRID; x++)
        {
            uint16_t i0 = (uint16_t)(z * WATER_SIDE + x);
            uint16_t i1 = (uint16_t)(i0 + 1);
            uint16_t i2 = (uint16_t)(i0 + WATER_SIDE);
            uint16_t i3 = (uint16_t)(i2 + 1);
            idx[k++] = i0; idx[k++] = i2; idx[k++] = i1;
            idx[k++] = i1; idx[k++] = i2; idx[k++] = i3;
        }
    }
    if (!g_water.ib.map) vkUnmapMemory(c->device, g_water.ib.mem);
    return create_pipeline(c, rp, multiview);
}

void water_record(VkCommandBuffer cmd, uint32_t slot)
{
    if (!g_water.core || g_water.pipe == VK_NULL_HANDLE) return;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_water.pipe);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_water.layout, 0, 1,
                            &g_scene.set0[slot], 0, NULL);
    vkCmdBindIndexBuffer(cmd, g_water.ib.buf, 0, VK_INDEX_TYPE_UINT16);
    vkCmdDrawIndexed(cmd, WATER_IDX, 1, 0, 0, 0);
}

void water_destroy(void)
{
    if (!g_water.core) return;
    if (g_water.pipe != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(g_water.core->device, g_water.pipe, NULL);
        g_water.pipe = VK_NULL_HANDLE;
    }
    if (g_water.ib.buf != VK_NULL_HANDLE) vkc_buffer_destroy(g_water.core, &g_water.ib);
    g_water.core = NULL;
}
