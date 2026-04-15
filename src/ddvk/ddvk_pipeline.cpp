#include "ddvk_pipeline.h"
#include "ddvk_renderer.h"
#include "ddvk_shaders.h"
#include <vector>

namespace ddvk {

//=============================================================================
// PipelineManager - реализация
//=============================================================================

PipelineManager::PipelineManager(VulkanRenderer* renderer)
    : m_renderer(renderer)
    , m_pipelineLayout(VK_NULL_HANDLE)
    , m_blitPipeline(VK_NULL_HANDLE)
    , m_fillPipeline(VK_NULL_HANDLE)
    , m_clearPipeline(VK_NULL_HANDLE)
    , m_descriptorSetLayout(VK_NULL_HANDLE)
    , m_descriptorPool(VK_NULL_HANDLE) {
}

PipelineManager::~PipelineManager() {
    DestroyPipelines();
    
    VkDevice device = m_renderer->GetDevice();
    
    if (m_descriptorPool) {
        vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
    }
    
    if (m_descriptorSetLayout) {
        vkDestroyDescriptorSetLayout(device, m_descriptorSetLayout, nullptr);
    }
}

bool PipelineManager::CreateBlitPipeline(VkRenderPass renderPass) {
    // Создаем пайплайн без использования ShaderManager
    PipelineInfo info;
    info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    info.polygonMode = VK_POLYGON_MODE_FILL;
    info.cullMode = VK_CULL_MODE_NONE;
    info.frontFace = VK_FRONT_FACE_CLOCKWISE;
    info.depthTest = false;
    info.depthWrite = false;
    info.blending = false;
    
    // Используем заглушки для шейдеров (в реальном коде нужно загрузить SPIR-V)
    std::vector<uint32_t> vertShader;
    std::vector<uint32_t> fragShader;
    
    m_blitPipeline = CreatePipeline(info, vertShader, fragShader, renderPass);
    
    return m_blitPipeline != VK_NULL_HANDLE;
}

bool PipelineManager::CreateFillPipeline(VkRenderPass renderPass) {
    PipelineInfo info;
    info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    info.polygonMode = VK_POLYGON_MODE_FILL;
    info.cullMode = VK_CULL_MODE_NONE;
    info.frontFace = VK_FRONT_FACE_CLOCKWISE;
    info.depthTest = false;
    info.depthWrite = false;
    info.blending = false;
    
    std::vector<uint32_t> vertShader;
    std::vector<uint32_t> fragShader;
    
    m_fillPipeline = CreatePipeline(info, vertShader, fragShader, renderPass);
    
    return m_fillPipeline != VK_NULL_HANDLE;
}

bool PipelineManager::CreateClearPipeline(VkRenderPass renderPass) {
    PipelineInfo info;
    info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    info.polygonMode = VK_POLYGON_MODE_FILL;
    info.cullMode = VK_CULL_MODE_NONE;
    info.frontFace = VK_FRONT_FACE_CLOCKWISE;
    info.depthTest = false;
    info.depthWrite = false;
    info.blending = false;
    
    std::vector<uint32_t> vertShader;
    std::vector<uint32_t> fragShader;
    
    m_clearPipeline = CreatePipeline(info, vertShader, fragShader, renderPass);
    
    return m_clearPipeline != VK_NULL_HANDLE;
}

VkPipeline PipelineManager::CreatePipeline(const PipelineInfo& info,
                                           const std::vector<uint32_t>& vertShader,
                                           const std::vector<uint32_t>& fragShader,
                                           VkRenderPass renderPass) {
    VkDevice device = m_renderer->GetDevice();
    
    if (!renderPass) {
        return VK_NULL_HANDLE;
    }
    
    // Создаем pipeline layout если его нет
    if (!m_pipelineLayout) {
        VkPipelineLayoutCreateInfo layoutInfo = {};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 0;
        layoutInfo.pushConstantRangeCount = 0;
        
        if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
            return VK_NULL_HANDLE;
        }
    }
    
    // Временно создаем заглушки для шейдеров (в реальном коде нужно загрузить настоящие)
    VkShaderModuleCreateInfo vertCreateInfo = {};
    vertCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vertCreateInfo.codeSize = vertShader.size() * sizeof(uint32_t);
    vertCreateInfo.pCode = vertShader.data();
    
    VkShaderModule vertModule = VK_NULL_HANDLE;
    if (vertCreateInfo.codeSize > 0) {
        vkCreateShaderModule(device, &vertCreateInfo, nullptr, &vertModule);
    }
    
    VkShaderModuleCreateInfo fragCreateInfo = {};
    fragCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    fragCreateInfo.codeSize = fragShader.size() * sizeof(uint32_t);
    fragCreateInfo.pCode = fragShader.data();
    
    VkShaderModule fragModule = VK_NULL_HANDLE;
    if (fragCreateInfo.codeSize > 0) {
        vkCreateShaderModule(device, &fragCreateInfo, nullptr, &fragModule);
    }
    
    // Шейдерные стадии
    std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
    
    if (vertModule) {
        VkPipelineShaderStageCreateInfo vertStageInfo = {};
        vertStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertStageInfo.module = vertModule;
        vertStageInfo.pName = "main";
        shaderStages.push_back(vertStageInfo);
    }
    
    if (fragModule) {
        VkPipelineShaderStageCreateInfo fragStageInfo = {};
        fragStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragStageInfo.module = fragModule;
        fragStageInfo.pName = "main";
        shaderStages.push_back(fragStageInfo);
    }
    
    // Vertex input (пустой - используем встроенные координаты в шейдере)
    VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    
    // Input assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = info.topology;
    inputAssembly.primitiveRestartEnable = VK_FALSE;
    
    // Viewport (будет динамическим)
    VkPipelineViewportStateCreateInfo viewportState = {};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    
    // Rasterizer
    VkPipelineRasterizationStateCreateInfo rasterizer = {};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = info.polygonMode;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = info.cullMode;
    rasterizer.frontFace = info.frontFace;
    rasterizer.depthBiasEnable = VK_FALSE;
    
    // Multisampling
    VkPipelineMultisampleStateCreateInfo multisampling = {};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    
    // Depth/stencil
    VkPipelineDepthStencilStateCreateInfo depthStencil = {};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = info.depthTest ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = info.depthWrite ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;
    
    // Color blending
    VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    
    if (info.blending) {
        colorBlendAttachment.blendEnable = VK_TRUE;
        colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    } else {
        colorBlendAttachment.blendEnable = VK_FALSE;
    }
    
    VkPipelineColorBlendStateCreateInfo colorBlending = {};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
    
    // Динамические состояния (viewport и scissor)
    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    
    VkPipelineDynamicStateCreateInfo dynamicState = {};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();
    
    // Создаем pipeline
    VkGraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;
    
    VkPipeline pipeline;
    VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
    
    // Очищаем шейдерные модули
    if (vertModule) {
        vkDestroyShaderModule(device, vertModule, nullptr);
    }
    if (fragModule) {
        vkDestroyShaderModule(device, fragModule, nullptr);
    }
    
    return (result == VK_SUCCESS) ? pipeline : VK_NULL_HANDLE;
}

void PipelineManager::DestroyPipelines() {
    VkDevice device = m_renderer->GetDevice();
    
    if (m_blitPipeline) {
        vkDestroyPipeline(device, m_blitPipeline, nullptr);
        m_blitPipeline = VK_NULL_HANDLE;
    }
    
    if (m_fillPipeline) {
        vkDestroyPipeline(device, m_fillPipeline, nullptr);
        m_fillPipeline = VK_NULL_HANDLE;
    }
    
    if (m_clearPipeline) {
        vkDestroyPipeline(device, m_clearPipeline, nullptr);
        m_clearPipeline = VK_NULL_HANDLE;
    }
    
    if (m_pipelineLayout) {
        vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
        m_pipelineLayout = VK_NULL_HANDLE;
    }
}

bool PipelineManager::CreateDescriptorSetLayout() {
    VkDevice device = m_renderer->GetDevice();
    
    VkDescriptorSetLayoutBinding samplerLayoutBinding = {};
    samplerLayoutBinding.binding = 0;
    samplerLayoutBinding.descriptorCount = 1;
    samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerLayoutBinding.pImmutableSamplers = nullptr;
    samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &samplerLayoutBinding;
    
    return vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_descriptorSetLayout) == VK_SUCCESS;
}

bool PipelineManager::CreateDescriptorPool() {
    VkDevice device = m_renderer->GetDevice();
    
    VkDescriptorPoolSize poolSize = {};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 1;
    
    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 1;
    
    return vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_descriptorPool) == VK_SUCCESS;
}

bool PipelineManager::CreateDescriptorSets() {
    VkDevice device = m_renderer->GetDevice();
    
    if (!m_descriptorSetLayout || !m_descriptorPool) {
        return false;
    }
    
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_descriptorSetLayout;
    
    m_descriptorSets.resize(1);
    return vkAllocateDescriptorSets(device, &allocInfo, m_descriptorSets.data()) == VK_SUCCESS;
}

VkDescriptorSet PipelineManager::GetDescriptorSet(uint32_t index) const {
    if (index < m_descriptorSets.size()) {
        return m_descriptorSets[index];
    }
    return VK_NULL_HANDLE;
}

} // namespace ddvk