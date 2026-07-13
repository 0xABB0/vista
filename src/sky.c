#include "vista.h"

static struct {
    VkDevice device;
    VkPipeline pipe;
    VkPipelineLayout layout;
} g_sky;

int sky_init(VkCore *c, VkRenderPass rp, bool multiview, const Scene *scene)
{
    VkShaderModule vs = vkc_shader(c, multiview ? "shaders/sky.vert.mv.spv" : "shaders/sky.vert.spv");
    VkShaderModule fs = vkc_shader(c, multiview ? "shaders/sky.frag.mv.spv" : "shaders/sky.frag.spv");
    if (vs == VK_NULL_HANDLE || fs == VK_NULL_HANDLE) {
        if (vs != VK_NULL_HANDLE) vkDestroyShaderModule(c->device, vs, NULL);
        if (fs != VK_NULL_HANDLE) vkDestroyShaderModule(c->device, fs, NULL);
        return -1;
    }

    VkPipelineShaderStageCreateInfo stages[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vs,
            .pName = "main",
            .pSpecializationInfo = vkc_tier_spec(c),
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = fs,
            .pName = "main",
            .pSpecializationInfo = vkc_tier_spec(c),
        },
    };

    VkPipelineVertexInputStateCreateInfo vin = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    };

    VkPipelineInputAssemblyStateCreateInfo ia = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };

    VkPipelineViewportStateCreateInfo vp = {
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
        .rasterizationSamples = c->msaa,
    };

    VkPipelineDepthStencilStateCreateInfo ds = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_FALSE,
        .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
    };

    VkPipelineColorBlendAttachmentState cba = {
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
        .pViewportState = &vp,
        .pRasterizationState = &rs,
        .pMultisampleState = &ms,
        .pDepthStencilState = &ds,
        .pColorBlendState = &cb,
        .pDynamicState = &dsi,
        .layout = scene->pipe_layout,
        .renderPass = rp,
        .subpass = 0,
    };

    VkResult r = vkCreateGraphicsPipelines(c->device, c->pcache, 1, &pci, NULL, &g_sky.pipe);
    vkDestroyShaderModule(c->device, vs, NULL);
    vkDestroyShaderModule(c->device, fs, NULL);
    if (r != VK_SUCCESS) return -1;

    g_sky.device = c->device;
    g_sky.layout = scene->pipe_layout;
    return 0;
}

void sky_record(VkCommandBuffer cmd, uint32_t slot)
{
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_sky.pipe);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_sky.layout, 0, 1,
                            &g_scene.set0[slot], 0, NULL);
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

void sky_destroy(void)
{
    if (g_sky.pipe != VK_NULL_HANDLE) {
        vkDestroyPipeline(g_sky.device, g_sky.pipe, NULL);
        g_sky.pipe = VK_NULL_HANDLE;
    }
}
